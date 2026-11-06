// Sockets owned by PooledNodes: their I/O is watched by the TaskPool's I/O
// thread and their callbacks run inside the node's strand.

#include <gtest/gtest.h>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Executor.h>
#include <SNFCore/NodePtr.h>
#include <SNFCore/PooledNode.h>
#include <SNFCore/TaskPool.h>
#include <SNFNetwork/HostAddress.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>
#include <SNFNetwork/UdpSocket.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class PooledSocketFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }
    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
};

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = 3s, std::function<void()> pump = {})
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (! predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        if (pump) {
            pump();
        }
        std::this_thread::sleep_for(200us);
    }
    return true;
}

// ── Plain POSIX clients: they never touch the framework ────────────────────

int connectTcp(std::uint16_t port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval timeout{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

std::string receiveExactly(int fd, std::size_t size)
{
    std::string received;
    char buffer[4096];
    while (received.size() < size) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;  // closed or timed out
        }
        received.append(buffer, static_cast<std::size_t>(n));
    }
    return received;
}

std::string tcpEcho(std::uint16_t port, const std::string& message)
{
    const int fd = connectTcp(port);
    if (fd < 0) {
        return "<connect failed>";
    }
    std::string echoed = sendAll(fd, message) ? receiveExactly(fd, message.size()) : "<send failed>";
    ::close(fd);
    return echoed;
}

// ── Pooled nodes under test ────────────────────────────────────────────────

struct EchoRecord {
    std::mutex mutex;
    int callbacks = 0;
    int callbacksOffOwner = 0;
    std::set<std::thread::id> threads;
    std::set<const Executor*> executors;
    std::chrono::milliseconds delayPerRead{0};

    void note(const Executor* owner)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++callbacks;
        if (! owner->isCurrent()) {
            ++callbacksOffOwner;
        }
        threads.insert(std::this_thread::get_id());
        executors.insert(owner);
    }
};

// Echoes everything its socket receives.
class EchoHandler final : public PooledNode
{
public:
    EchoHandler(TcpSocket* socket, std::shared_ptr<EchoRecord> record) : m_socket(socket), m_record(std::move(record))
    {
        m_socket->setParent(this);
        m_socket->readyRead.connect(NodePtr<EchoHandler>(this), &EchoHandler::onReadyRead);
        m_socket->disconnected.connect(NodePtr<EchoHandler>(this), [](EchoHandler& self) { self.deleteLater(); });
    }

    void onReadyRead()
    {
        m_record->note(ownerExecutor());
        if (m_record->delayPerRead.count() > 0) {
            std::this_thread::sleep_for(m_record->delayPerRead);
        }
        m_socket->write(m_socket->readAll());
    }

private:
    TcpSocket* m_socket;
    std::shared_ptr<EchoRecord> m_record;
};

// Accepts connections inside its own strand and gives each client its own
// pooled EchoHandler (created inside the strand, then moved to a new one).
class PooledEchoServer final : public PooledNode
{
public:
    PooledEchoServer(TaskPool* pool, std::shared_ptr<EchoRecord> record)
        : server(new TcpServer(this)), m_pool(pool), m_record(std::move(record))
    {
        server->newConnection.connect(NodePtr<PooledEchoServer>(this), &PooledEchoServer::onNewConnection);
    }

    PooledEchoServer(const PooledEchoServer&) = delete;
    PooledEchoServer& operator=(const PooledEchoServer&) = delete;

    void onNewConnection()
    {
        acceptedInsideOwner.store(ownerExecutor()->isCurrent());
        while (TcpSocket* socket = server->nextPendingConnection()) {
            auto* handler = new EchoHandler(socket, m_record);  // a root of this strand...
            // Once moved, the handler belongs to another strand and may be
            // destroyed there at any moment: only look at it through a
            // NodePtr, which reads the owner from the control block.
            const NodePtr<EchoHandler> handlerPtr(handler);
            if (! handler->moveToTaskPool(m_pool)) {  // ...moved to a strand of its own
                movesRejected.fetch_add(1);
            }
            if (handlerPtr.ownerExecutor() != ownerExecutor()) {
                ownStrand.fetch_add(1);
            }
            accepted.fetch_add(1);
        }
    }

    TcpServer* server;
    std::atomic<int> accepted{0};
    std::atomic<int> movesRejected{0};
    std::atomic<int> ownStrand{0};  // handlers that left the server's strand
    std::atomic<bool> acceptedInsideOwner{false};

private:
    TaskPool* m_pool;
    std::shared_ptr<EchoRecord> m_record;
};

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledSocketFixture, socketsNoLongerRequireAnEventLoop)
{
    auto* tcp = new TcpSocket();
    auto* udp = new UdpSocket();
    auto* server = new TcpServer();
    EXPECT_FALSE(tcp->requiresEventLoop());
    EXPECT_FALSE(udp->requiresEventLoop());
    EXPECT_FALSE(server->requiresEventLoop());
    delete tcp;
    delete udp;
    delete server;
}

TEST_F(PooledSocketFixture, pooledServerEchoesManyClientsEachOnItsOwnStrand)
{
    TaskPool pool(4);
    auto record = std::make_shared<EchoRecord>();
    record->delayPerRead = 2ms;  // makes handlers overlap, so several workers are used
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));  // listening fd registered on main...
    const std::uint16_t port = echo->server->serverPort();
    ASSERT_GT(port, 0);
    ASSERT_TRUE(echo->moveToTaskPool(&pool));  // ...and moved to the pool's I/O thread

    constexpr int kClients = 16;
    std::vector<std::string> results(kClients);
    std::vector<std::thread> clients;
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([&, i]() { results[i] = tcpEcho(port, "client-" + std::to_string(i)); });
    }
    for (std::thread& client : clients) {
        client.join();
    }

    for (int i = 0; i < kClients; ++i) {
        EXPECT_EQ(results[i], "client-" + std::to_string(i));
    }
    EXPECT_EQ(echo->accepted.load(), kClients);
    EXPECT_EQ(echo->movesRejected.load(), 0);
    EXPECT_EQ(echo->ownStrand.load(), kClients);  // every client got a strand of its own
    EXPECT_TRUE(echo->acceptedInsideOwner.load());

    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_GE(record->callbacks, kClients);
    EXPECT_EQ(record->callbacksOffOwner, 0);
    // Strands of closed clients are recycled, so fewer distinct strands than
    // clients is fine; but the work did spread over several of them.
    EXPECT_GT(record->executors.size(), 1u);
    EXPECT_GT(record->threads.size(), 1u);
}

TEST_F(PooledSocketFixture, serverStartedFromOutsideAfterMovingIntoThePoolListensThere)
{
    TaskPool pool(2);
    auto record = std::make_shared<EchoRecord>();
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->moveToTaskPool(&pool));

    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));  // posted to the strand
    ASSERT_TRUE(waitUntil([&]() { return echo->server->serverPort() > 0; }));

    EXPECT_EQ(tcpEcho(echo->server->serverPort(), "hello"), "hello");
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->callbacksOffOwner, 0);
}

TEST_F(PooledSocketFixture, acceptorOnMainHandsEachConnectionToAPooledHandler)
{
    // The "acceptor on a thread, handlers in the pool" pattern.
    TaskPool pool(4);
    auto record = std::make_shared<EchoRecord>();
    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    std::atomic<int> handed{0};
    server.newConnection.connect([&]() {
        while (TcpSocket* socket = server.nextPendingConnection()) {
            auto* handler = new EchoHandler(socket, record);  // on main
            if (handler->moveToTaskPool(&pool)) {             // the socket moves with it
                handed.fetch_add(1);
            }
        }
    });

    constexpr int kClients = 8;
    std::vector<std::string> results(kClients);
    std::vector<std::thread> clients;
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([&, i]() { results[i] = tcpEcho(server.serverPort(), "m" + std::to_string(i)); });
    }
    // Main only runs the acceptor; the echoes happen in the pool.
    const bool allDone = waitUntil(
        [&]() {
            std::lock_guard<std::mutex> lock(record->mutex);
            return record->callbacks >= kClients;
        },
        5s, [&]() { app->loopOnce(); });
    for (std::thread& client : clients) {
        client.join();
    }

    ASSERT_TRUE(allDone);
    EXPECT_EQ(handed.load(), kClients);
    for (int i = 0; i < kClients; ++i) {
        EXPECT_EQ(results[i], "m" + std::to_string(i));
    }
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->callbacksOffOwner, 0);
}

TEST_F(PooledSocketFixture, slowHandlerStillReceivesEveryChunk)
{
    // While a read callback runs, the descriptor stays disarmed; once it
    // returns it is re-armed, so data that arrived meanwhile is not lost.
    TaskPool pool(2);
    auto record = std::make_shared<EchoRecord>();
    record->delayPerRead = 15ms;
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
    ASSERT_TRUE(echo->moveToTaskPool(&pool));

    const int fd = connectTcp(echo->server->serverPort());
    ASSERT_GE(fd, 0);
    std::string expected;
    for (int i = 0; i < 10; ++i) {
        const std::string chunk = "chunk" + std::to_string(i) + ";";
        expected += chunk;
        ASSERT_TRUE(sendAll(fd, chunk));
        std::this_thread::sleep_for(3ms);
    }
    EXPECT_EQ(receiveExactly(fd, expected.size()), expected);
    ::close(fd);
}

TEST_F(PooledSocketFixture, udpSocketInThePoolReceivesAndReplies)
{
    class UdpEcho final : public PooledNode
    {
    public:
        explicit UdpEcho(std::shared_ptr<EchoRecord> record) : socket(new UdpSocket(false, this)), m_record(std::move(record))
        {
            socket->readyRead.connect(NodePtr<UdpEcho>(this), &UdpEcho::onReadyRead);
        }

        UdpEcho(const UdpEcho&) = delete;
        UdpEcho& operator=(const UdpEcho&) = delete;

        void onReadyRead()
        {
            m_record->note(ownerExecutor());
            while (socket->hasPendingDatagram()) {
                const NetworkDatagram datagram = socket->pendingDatagram();
                socket->sendDatagram(datagram.data().toString(), HostAddress(datagram.senderHost()),
                                     datagram.senderPort());
            }
        }

        UdpSocket* socket;

    private:
        std::shared_ptr<EchoRecord> m_record;
    };

    TaskPool pool(2);
    auto record = std::make_shared<EchoRecord>();
    auto* echo = new UdpEcho(record);
    ASSERT_TRUE(echo->socket->bind(HostAddress::LocalHost, 0));
    const std::uint16_t port = echo->socket->boundPort();
    ASSERT_GT(port, 0);
    ASSERT_TRUE(echo->moveToTaskPool(&pool));

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(fd, 0);
    timeval timeout{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    const std::string message = "datagram";
    ASSERT_EQ(::sendto(fd, message.data(), message.size(), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
              static_cast<ssize_t>(message.size()));
    char buffer[64] = {};
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    ::close(fd);

    ASSERT_EQ(n, static_cast<ssize_t>(message.size()));
    EXPECT_EQ(std::string(buffer, static_cast<std::size_t>(n)), message);
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->callbacksOffOwner, 0);
}

TEST_F(PooledSocketFixture, pooledServerMovedBackToMainKeepsServing)
{
    TaskPool pool(2);
    auto record = std::make_shared<EchoRecord>();
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = echo->server->serverPort();
    ASSERT_TRUE(echo->moveToTaskPool(&pool));
    EXPECT_EQ(tcpEcho(port, "in pool"), "in pool");

    ASSERT_TRUE(echo->moveToThread(std::this_thread::get_id()));  // posted to the strand
    pool.waitForDone();
    ASSERT_NE(echo->ownerEventLoop(), nullptr);
    EXPECT_EQ(pool.ioWatchCount(), 0u);  // the listening fd left the pool's I/O thread

    // Accepting now happens on main (the handlers still go to the pool).
    std::string result;
    std::thread client([&]() { result = tcpEcho(port, "on main"); });
    const bool accepted = waitUntil([&]() { return echo->accepted.load() == 2; }, 3s, [&]() { app->loopOnce(); });
    client.join();
    ASSERT_TRUE(accepted);
    EXPECT_EQ(result, "on main");
}

TEST_F(PooledSocketFixture, closedConnectionsUnregisterTheirDescriptors)
{
    TaskPool pool(2);
    auto record = std::make_shared<EchoRecord>();
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
    ASSERT_TRUE(echo->moveToTaskPool(&pool));
    pool.waitForDone();
    EXPECT_EQ(pool.ioWatchCount(), 1u);  // the listening socket

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(tcpEcho(echo->server->serverPort(), "x"), "x");  // client closes afterwards
    }
    // Each handler deletes itself on disconnect; only the server remains.
    ASSERT_TRUE(waitUntil([&]() { return pool.ioWatchCount() == 1u; }));

    echo->deleteLater();
    pool.waitForDone();
    EXPECT_EQ(pool.ioWatchCount(), 0u);
}

TEST_F(PooledSocketFixture, shutdownWithOpenPooledConnectionsReturnsAndClosesThem)
{
    auto record = std::make_shared<EchoRecord>();
    int fd = -1;
    std::atomic<bool> finished{false};
    {
        TaskPool pool(2);
        auto* echo = new PooledEchoServer(&pool, record);
        ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
        ASSERT_TRUE(echo->moveToTaskPool(&pool));
        fd = connectTcp(echo->server->serverPort());
        ASSERT_GE(fd, 0);
        ASSERT_TRUE(sendAll(fd, "open"));
        ASSERT_EQ(receiveExactly(fd, 4), "open");  // the connection is alive in the pool

        std::thread stopper([&]() {
            pool.shutdown();
            finished.store(true);
        });
        const bool done = waitUntil([&]() { return finished.load(); }, 5s);
        if (done) {
            stopper.join();
        } else {
            stopper.detach();
        }
        ASSERT_TRUE(done) << "TaskPool::shutdown() hung with open pooled connections";
        EXPECT_EQ(pool.ioWatchCount(), 0u);
    }
    // The pooled socket was destroyed, so the peer sees the connection close.
    char byte = 0;
    EXPECT_EQ(::recv(fd, &byte, 1, 0), 0);
    ::close(fd);
}

TEST_F(PooledSocketFixture, stressManyConcurrentClientsAgainstAPooledServer)
{
    TaskPool pool(4);
    auto record = std::make_shared<EchoRecord>();
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
    echo->server->setMaxPendingConnections(256);
    ASSERT_TRUE(echo->moveToTaskPool(&pool));
    const std::uint16_t port = echo->server->serverPort();

    constexpr int kThreads = 8;
    constexpr int kRoundsPerThread = 10;
    std::atomic<int> ok{0};
    std::vector<std::thread> clients;
    for (int t = 0; t < kThreads; ++t) {
        clients.emplace_back([&, t]() {
            for (int r = 0; r < kRoundsPerThread; ++r) {
                const std::string message = "t" + std::to_string(t) + "r" + std::to_string(r) + std::string(512, 'x');
                if (tcpEcho(port, message) == message) {
                    ok.fetch_add(1);
                }
            }
        });
    }
    for (std::thread& client : clients) {
        client.join();
    }

    EXPECT_EQ(ok.load(), kThreads * kRoundsPerThread);
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->callbacksOffOwner, 0);
}

TEST_F(PooledSocketFixture, largeEchoUsesWriteReadinessInThePool)
{
    // 2 MB cannot be written in one go: the socket keeps a write queue and asks
    // the pool's I/O thread for write readiness (modifyIO) until it drains.
    TaskPool pool(2);
    auto record = std::make_shared<EchoRecord>();
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
    ASSERT_TRUE(echo->moveToTaskPool(&pool));

    const int fd = connectTcp(echo->server->serverPort());
    ASSERT_GE(fd, 0);
    std::string payload(2 * 1024 * 1024, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('a' + (i % 26));
    }
    std::string echoed;
    std::thread reader([&]() { echoed = receiveExactly(fd, payload.size()); });
    ASSERT_TRUE(sendAll(fd, payload));
    reader.join();
    ::close(fd);

    EXPECT_EQ(echoed.size(), payload.size());
    EXPECT_TRUE(echoed == payload);
}

TEST_F(PooledSocketFixture, dataArrivingAfterShutdownStartedIsNotDelivered)
{
    auto record = std::make_shared<EchoRecord>();
    std::atomic<int> readsAfterShutdown{0};
    std::atomic<bool> shuttingDown{false};
    {
        TaskPool pool(1);
        auto* echo = new PooledEchoServer(&pool, record);
        ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
        ASSERT_TRUE(echo->moveToTaskPool(&pool));
        const int fd = connectTcp(echo->server->serverPort());
        ASSERT_GE(fd, 0);
        ASSERT_TRUE(sendAll(fd, "first"));
        ASSERT_EQ(receiveExactly(fd, 5), "first");

        // Keep the only worker busy so shutdown() starts while it runs; then
        // send more data: its readiness must not reach the strand any more.
        std::promise<void> busy;
        ASSERT_TRUE(pool.start([&busy]() {
            busy.set_value();
            std::this_thread::sleep_for(50ms);
        }));
        busy.get_future().wait();
        {
            std::lock_guard<std::mutex> lock(record->mutex);
            record->callbacks = 0;
        }
        std::thread late([&]() {
            std::this_thread::sleep_for(15ms);
            sendAll(fd, "late");
        });
        shuttingDown.store(true);
        pool.shutdown();
        late.join();
        std::lock_guard<std::mutex> lock(record->mutex);
        readsAfterShutdown.store(record->callbacks);
        ::close(fd);
    }
    EXPECT_TRUE(shuttingDown.load());
    EXPECT_EQ(readsAfterShutdown.load(), 0);
}

TEST_F(PooledSocketFixture, largeReplyToASmallRequestIsFlushedThroughWriteReadiness)
{
    // The client sends one byte and reads the 4 MB reply slowly: the socket
    // cannot write it at once and nothing more arrives to retry on, so it
    // must be woken up by write readiness (modifyIO with EPOLLOUT).
    class BigReply final : public PooledNode
    {
    public:
        explicit BigReply(TcpSocket* socket) : m_socket(socket)
        {
            m_socket->setParent(this);
            m_socket->readyRead.connect(NodePtr<BigReply>(this), [](BigReply& self) {
                self.m_socket->readAll();
                self.m_socket->write(std::string(4 * 1024 * 1024, 'z'));
            });
        }

    private:
        TcpSocket* m_socket;
    };

    TaskPool pool(2);
    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    std::atomic<bool> handed{false};
    server.newConnection.connect([&]() {
        while (TcpSocket* socket = server.nextPendingConnection()) {
            handed.store((new BigReply(socket))->moveToTaskPool(&pool));
        }
    });

    const int fd = connectTcp(server.serverPort());
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(waitUntil([&]() { return handed.load(); }, 3s, [&]() { app->loopOnce(); }));
    ASSERT_TRUE(sendAll(fd, "?"));
    std::this_thread::sleep_for(100ms);  // let the kernel buffers fill up
    const std::string reply = receiveExactly(fd, 4 * 1024 * 1024);
    ::close(fd);

    EXPECT_EQ(reply.size(), 4u * 1024 * 1024);
}

TEST_F(PooledSocketFixture, aSlowCallbackDoesNotMakeTheIoThreadSpin)
{
    // While a read callback is queued or running, more data is pending on the
    // descriptor. The I/O thread must wait (the descriptor is disarmed), not
    // spin on a descriptor that stays readable.
    TaskPool pool(1);
    auto record = std::make_shared<EchoRecord>();
    record->delayPerRead = 300ms;
    auto* echo = new PooledEchoServer(&pool, record);
    ASSERT_TRUE(echo->server->listen(HostAddress::LocalHost, 0));
    ASSERT_TRUE(echo->moveToTaskPool(&pool));
    const int fd = connectTcp(echo->server->serverPort());
    ASSERT_GE(fd, 0);

    ASSERT_TRUE(sendAll(fd, "a"));
    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(record->mutex);
        return record->callbacks > 0;
    }));
    ASSERT_TRUE(sendAll(fd, "b"));  // readable while the callback sleeps
    std::this_thread::sleep_for(20ms);

    auto cpuTime = []() {
        rusage usage{};
        ::getrusage(RUSAGE_SELF, &usage);
        return std::chrono::microseconds(usage.ru_utime.tv_sec * 1000000L + usage.ru_utime.tv_usec
                                         + usage.ru_stime.tv_sec * 1000000L + usage.ru_stime.tv_usec);
    };
    const auto cpuBefore = cpuTime();
    std::this_thread::sleep_for(150ms);  // everything should be asleep now
    const auto cpuUsed = cpuTime() - cpuBefore;

    EXPECT_EQ(receiveExactly(fd, 2), "ab");
    ::close(fd);
    EXPECT_LT(cpuUsed, std::chrono::microseconds(50000)) << "the I/O thread kept spinning";
}
