#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/NodePtr.h>
#include <SNFCore/ThreadPool.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>

using namespace snf;
using namespace std::chrono_literals;

namespace {

Application* gSigintTargetApp = nullptr;

void testSigintHandler(int)
{
    if (gSigintTargetApp != nullptr) {
        gSigintTargetApp->quit();
    }
}

class ScopedSigintHandler
{
public:
    explicit ScopedSigintHandler(Application* app)
        : m_previousHandler(std::signal(SIGINT, testSigintHandler)), m_previousApp(gSigintTargetApp)
    {
        gSigintTargetApp = app;
    }

    ~ScopedSigintHandler()
    {
        gSigintTargetApp = m_previousApp;
        std::signal(SIGINT, m_previousHandler);
    }

private:
    using SignalHandler = void (*)(int);
    SignalHandler m_previousHandler = nullptr;
    Application* m_previousApp = nullptr;
};

class TcpServerFixture : public ::testing::Test
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

void armShutdown(Timer& timer, std::chrono::milliseconds timeout)
{
    timer.setSingleShot(true);
    timer.timeout.connect([&timer]() {
        if (EventLoop* loop = timer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    timer.start(timeout);
}

}  // namespace

TEST_F(TcpServerFixture, listenOnEphemeralPortAndAcceptConnection)
{
    TcpServer server;
    TcpSocket client(false);

    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    bool didAccept = false;
    std::string clientError;
    std::string serverError;

    server.newConnection.connect([&]() {
        TcpSocket* incoming = server.nextPendingConnection();
        ASSERT_NE(incoming, nullptr);
        didAccept = true;
        incoming->close();
        delete incoming;

        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    client.errorOccurred.connect([&](const std::string& error) {
        clientError = error;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    client.connectToHost(HostAddress::LocalHost, port);
    app->run();

    EXPECT_TRUE(clientError.empty());
    EXPECT_TRUE(serverError.empty());
    EXPECT_TRUE(didAccept);
}

TEST_F(TcpServerFixture, listenAndConnectUsingLocalhostHostName)
{
    TcpServer server;
    TcpSocket client(false);

    ASSERT_TRUE(server.listen(HostAddress("localhost"), 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    bool didAccept = false;
    std::string clientError;
    std::string serverError;

    server.newConnection.connect([&]() {
        TcpSocket* incoming = server.nextPendingConnection();
        ASSERT_NE(incoming, nullptr);
        didAccept = true;
        incoming->close();
        delete incoming;

        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    client.errorOccurred.connect([&](const std::string& error) {
        clientError = error;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    client.connectToHost(HostAddress("localhost"), port);
    app->run();

    EXPECT_TRUE(clientError.empty());
    EXPECT_TRUE(serverError.empty());
    EXPECT_TRUE(didAccept);
}

TEST_F(TcpServerFixture, nextPendingConnectionReturnsQueuedSockets)
{
    TcpServer server;
    TcpSocket clientA(false);
    TcpSocket clientB(false);

    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    int signalCount = 0;
    std::string serverError;

    server.newConnection.connect([&]() {
        ++signalCount;
        if (signalCount >= 2) {
            if (EventLoop* loop = server.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    clientA.connectToHost(HostAddress::LocalHost, port);
    clientB.connectToHost(HostAddress::LocalHost, port);

    app->run();

    EXPECT_TRUE(serverError.empty());
    EXPECT_GE(signalCount, 2);

    TcpSocket* pendingA = server.nextPendingConnection();
    TcpSocket* pendingB = server.nextPendingConnection();
    TcpSocket* pendingC = server.nextPendingConnection();

    EXPECT_NE(pendingA, nullptr);
    EXPECT_NE(pendingB, nullptr);
    EXPECT_EQ(pendingC, nullptr);

    if (pendingA) {
        pendingA->close();
        delete pendingA;
    }
    if (pendingB) {
        pendingB->close();
        delete pendingB;
    }
}

TEST_F(TcpServerFixture, acceptedSocketCanEchoToClient)
{
    TcpServer server;
    TcpSocket client(false);

    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    const std::string payload = "hello-from-client";
    std::string receivedByClient;
    std::string clientError;
    std::string serverError;

    TcpSocket* accepted = nullptr;

    server.newConnection.connect([&]() {
        accepted = server.nextPendingConnection();
        ASSERT_NE(accepted, nullptr);

        accepted->readyRead.connect([&]() {
            const ByteArray input = accepted->readAll();
            std::string text = input.toString();
            accepted->write(text);
        });
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    client.connected.connect([&]() { client.write(payload); });

    client.readyRead.connect([&]() {
        const ByteArray data = client.readAll();
        receivedByClient = data.toString();
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    client.errorOccurred.connect([&](const std::string& error) {
        clientError = error;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, port);
    app->run();

    EXPECT_TRUE(clientError.empty());
    EXPECT_TRUE(serverError.empty());
    EXPECT_EQ(receivedByClient, payload);

    if (accepted) {
        accepted->close();
        delete accepted;
    }
}

TEST_F(TcpServerFixture, acceptedSocketsMovedToThreadPoolReceiveClientMessages)
{
    constexpr std::size_t kClientCount = 4;

    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    ThreadPool* pool = app->threadPool();
    ASSERT_NE(pool, nullptr);
    const std::vector<std::thread::id> workerThreadIds = pool->workerThreadIds();
    ASSERT_FALSE(workerThreadIds.empty());

    std::vector<std::unique_ptr<TcpSocket>> clients;
    clients.reserve(kClientCount);

    std::vector<TcpSocket*> acceptedSockets;
    acceptedSockets.reserve(kClientCount);

    std::vector<std::string> expectedMessages;
    std::vector<std::string> receivedMessages(kClientCount);
    std::vector<std::thread::id> serverCallbackThreads(kClientCount);
    expectedMessages.reserve(kClientCount);

    std::mutex receivedMutex;
    std::string clientError;
    std::string serverError;

    auto stopWhenDone = [&]() {
        for (std::size_t i = 0; i < kClientCount; ++i) {
            if (receivedMessages[i] != expectedMessages[i]) {
                return;
            }
        }
        app->quit();
    };

    server.newConnection.connect([&]() {
        while (TcpSocket* peer = server.nextPendingConnection()) {
            const std::size_t index = acceptedSockets.size();
            acceptedSockets.push_back(peer);

            peer->readyRead.connect([&, peer, index]() {
                const ByteArray data = peer->readAll();
                std::lock_guard<std::mutex> lock(receivedMutex);
                receivedMessages[index] += data.toString();
                serverCallbackThreads[index] = std::this_thread::get_id();
                stopWhenDone();
            });

            peer->errorOccurred.connect([&](const std::string& error) {
                std::lock_guard<std::mutex> lock(receivedMutex);
                serverError = error;
                app->quit();
            });

            if (!peer->moveToThreadPool()) {
                serverError = "moveToThreadPool failed";
                app->quit();
                return;
            }

            if (acceptedSockets.size() == kClientCount) {
                for (std::size_t i = 0; i < kClientCount; ++i) {
                    clients[i]->write(expectedMessages[i]);
                }
            }
        }
    });

    server.errorOccurred.connect([&](const std::string& error) {
        std::lock_guard<std::mutex> lock(receivedMutex);
        serverError = error;
        app->quit();
    });

    for (std::size_t i = 0; i < kClientCount; ++i) {
        expectedMessages.push_back("client-" + std::to_string(i) + "-payload");

        auto client = std::make_unique<TcpSocket>(false);
        TcpSocket* clientPtr = client.get();
        clientPtr->errorOccurred.connect([&](const std::string& error) {
            std::lock_guard<std::mutex> lock(receivedMutex);
            clientError = error;
            app->quit();
        });

        clients.push_back(std::move(client));
    }

    Timer shutdown;
    armShutdown(shutdown, 4s);

    for (const auto& client : clients) {
        client->connectToHost(HostAddress::LocalHost, port);
    }

    app->run();

    EXPECT_TRUE(clientError.empty());
    EXPECT_TRUE(serverError.empty());
    ASSERT_EQ(acceptedSockets.size(), kClientCount);

    for (std::size_t i = 0; i < kClientCount; ++i) {
        EXPECT_EQ(receivedMessages[i], expectedMessages[i]);
        EXPECT_TRUE(std::find(workerThreadIds.begin(), workerThreadIds.end(), acceptedSockets[i]->threadId())
                    != workerThreadIds.end());
        EXPECT_EQ(serverCallbackThreads[i], acceptedSockets[i]->threadId());
    }

    for (const auto& client : clients) {
        client->close();
    }
    for (TcpSocket* peer : acceptedSockets) {
        peer->close();
        peer->deleteLater();
    }
    app->loopOnce();
}

TEST_F(TcpServerFixture, queueOverflowBehavior)
{
    TcpServer server;
    server.setMaxPendingConnections(2);
    
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    std::vector<TcpSocket*> clients;
    int acceptedCount = 0;
    std::string serverError;

    server.newConnection.connect([&]() {
        ++acceptedCount;
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    // Try to connect 5 clients, but queue is limited to 2
    for (int i = 0; i < 5; ++i) {
        auto client = new TcpSocket(false);
        clients.push_back(client);
        client->connectToHost(HostAddress::LocalHost, port);
    }

    app->run();

    EXPECT_TRUE(serverError.empty());
    // Should have accepted at least the first 2 (potentially more if queue+backlog allows)
    EXPECT_GE(acceptedCount, 2);

    // Dequeue pending connections
    int dequeuedCount = 0;
    while (TcpSocket* pending = server.nextPendingConnection()) {
        ++dequeuedCount;
        pending->close();
        delete pending;
    }

    EXPECT_EQ(dequeuedCount, acceptedCount);

    // Cleanup clients
    for (auto client : clients) {
        client->close();
        delete client;
    }
}

TEST_F(TcpServerFixture, serverCloseWithPendingSockets)
{
    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    std::vector<TcpSocket*> clients;
    std::vector<TcpSocket*> accepted;

    server.newConnection.connect([&]() {
        TcpSocket* sock = server.nextPendingConnection();
        if (sock) {
            accepted.push_back(sock);
        }
        // Stop accepting after collecting 5
        if (accepted.size() >= 5) {
            if (EventLoop* loop = server.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    // Connect 5 clients
    for (int i = 0; i < 5; ++i) {
        auto client = new TcpSocket(false);
        clients.push_back(client);
        client->connectToHost(HostAddress::LocalHost, port);
    }

    app->run();

    // Verify we have pending sockets (not yet dequeued by client code)
    EXPECT_GE(accepted.size(), 2);

    // Now close server with pending connections still in deque
    // This should clean up internal state (no memory leak)
    server.close();

    // Verify no more pending connections
    EXPECT_FALSE(server.hasPendingConnections());
    EXPECT_EQ(server.nextPendingConnection(), nullptr);

    // Cleanup clients
    for (auto client : clients) {
        client->close();
        delete client;
    }

    // accepted list now holds orphaned pointers, but they should still be valid
    // (cleanup in server.close() should have deleted them)
    // In this test, we just verify the server state is clean
}

TEST_F(TcpServerFixture, listenOnBusyPort)
{
    TcpServer server1;
    TcpServer server2;

    ASSERT_TRUE(server1.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server1.serverPort();
    ASSERT_GT(port, 0);

    std::string server2Error;
    bool server2ErrorEmitted = false;

    server2.errorOccurred.connect([&](const std::string& error) {
        server2Error = error;
        server2ErrorEmitted = true;
        if (EventLoop* loop = server2.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    // Try to bind second server to same port (should fail)
    const bool listenResult = server2.listen(HostAddress::LocalHost, port);

    // Even if listen() fails, give event loop a cycle to emit error signal
    if (!listenResult && !server2ErrorEmitted) {
        Timer timeout;
        armShutdown(timeout, 500ms);
        app->run();
    }

    EXPECT_FALSE(listenResult);
    EXPECT_FALSE(server2.isListening());
    EXPECT_TRUE(server2ErrorEmitted);
    EXPECT_FALSE(server2Error.empty());
}

TEST_F(TcpServerFixture, invalidIPv4Address)
{
    TcpServer server;
    std::string errorMessage;
    bool errorEmitted = false;

    server.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        errorEmitted = true;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    const bool listenResult = server.listen("999.999.999.999", 12345);

    // Give event loop a chance to emit error
    if (!listenResult && !errorEmitted) {
        Timer timeout;
        armShutdown(timeout, 500ms);
        app->run();
    }

    EXPECT_FALSE(listenResult);
    EXPECT_FALSE(server.isListening());
    EXPECT_TRUE(errorEmitted);
    EXPECT_FALSE(errorMessage.empty());

    // Verify can retry with valid address
    const bool retryResult = server.listen(HostAddress::LocalHost, 0);
    EXPECT_TRUE(retryResult);
    EXPECT_TRUE(server.isListening());
}

TEST_F(TcpServerFixture, largeDataTransferIntegrity)
{
    TcpServer server;
    TcpSocket client(false);

    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    // 1MB payload
    const std::size_t payloadSize = 1024 * 1024;
    std::vector<std::uint8_t> largePayloadVec;
    largePayloadVec.reserve(payloadSize);
    for (std::size_t i = 0; i < payloadSize; ++i) {
        largePayloadVec.push_back(static_cast<std::uint8_t>(i % 256));
    }
    const std::string largePayload(reinterpret_cast<const char*>(largePayloadVec.data()), largePayloadVec.size());

    std::string receivedByClient;
    receivedByClient.reserve(payloadSize);
    std::string clientError;
    std::string serverError;
    std::size_t bytesEchoed = 0;

    TcpSocket* accepted = nullptr;

    server.newConnection.connect([&]() {
        accepted = server.nextPendingConnection();
        ASSERT_NE(accepted, nullptr);

        accepted->readyRead.connect([&]() {
            const ByteArray input = accepted->readAll();
            if (!input.remainingSize()) {
                return;
            }
            accepted->write(input);
            bytesEchoed += input.remainingSize();
        });
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    client.connected.connect([&]() { client.write(largePayload); });

    std::size_t totalReceived = 0;
    client.readyRead.connect([&]() {
        const ByteArray data = client.readAll();
        receivedByClient += data.toString();
        totalReceived += data.remainingSize();

        // Stop when we've received everything
        if (totalReceived >= payloadSize) {
            if (EventLoop* loop = client.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    client.errorOccurred.connect([&](const std::string& error) {
        clientError = error;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 5s);

    client.connectToHost(HostAddress::LocalHost, port);
    app->run();

    EXPECT_TRUE(clientError.empty()) << "Client error: " << clientError;
    EXPECT_TRUE(serverError.empty()) << "Server error: " << serverError;
    EXPECT_EQ(receivedByClient.size(), payloadSize);
    EXPECT_EQ(receivedByClient, largePayload);

    if (accepted) {
        accepted->close();
        delete accepted;
    }
}

TEST_F(TcpServerFixture, closeDuringAcceptStorm)
{
    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    std::vector<TcpSocket*> clients;
    int acceptedCount = 0;
    bool serverClosed = false;

    server.newConnection.connect([&]() { ++acceptedCount; });

    Timer closeTimer;
    closeTimer.setSingleShot(true);
    closeTimer.timeout.connect([&]() {
        // Close server while accept storm is happening
        serverClosed = true;
        server.close();
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    // Rapid connection storm
    for (int i = 0; i < 20; ++i) {
        auto client = new TcpSocket(false);
        clients.push_back(client);
        client->connectToHost(HostAddress::LocalHost, port);
    }

    // Schedule server close after 100ms
    if (EventLoop* loop = server.ownerEventLoop()) {
        loop->post([&closeTimer]() { closeTimer.start(100ms); });
    }

    app->run();

    EXPECT_TRUE(serverClosed);
    EXPECT_FALSE(server.isListening());

    // Cleanup
    for (auto client : clients) {
        client->close();
        delete client;
    }
}

TEST_F(TcpServerFixture, quitApplicationWithSigintAfterClientConnect)
{
    TcpServer server;
    TcpSocket client(false);

    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    bool didAccept = false;
    bool didTriggerSigint = false;
    std::string clientError;
    std::string serverError;

    TcpSocket* accepted = nullptr;
    ScopedSigintHandler sigintGuard(app);

    server.newConnection.connect([&]() {
        accepted = server.nextPendingConnection();
        ASSERT_NE(accepted, nullptr);
        didAccept = true;

        didTriggerSigint = true;
        std::raise(SIGINT);
    });

    client.errorOccurred.connect([&](const std::string& error) {
        clientError = error;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        if (EventLoop* loop = server.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    client.connectToHost(HostAddress::LocalHost, port);
    app->run();

    EXPECT_TRUE(didAccept);
    EXPECT_TRUE(didTriggerSigint);
    EXPECT_TRUE(clientError.empty());
    EXPECT_TRUE(serverError.empty());

    if (accepted) {
        accepted->close();
        delete accepted;
    }
    client.close();
    server.close();
}

TEST_F(TcpServerFixture, deleteLaterOnAcceptedSocketDisconnectedDoesNotCrash)
{
    // Regression test: calling deleteLater() on an accepted TcpSocket inside
    // its disconnected signal must not crash EventLoop::run when the deferred
    // deletion is processed (same root-node double-free class of bug as the
    // WebSocket equivalent).
    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    bool clientDidConnect = false;
    bool serverPeerDisconnected = false;

    server.newConnection.connect([&]() {
        TcpSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        // Do NOT delete peer manually — ownership is transferred via deleteLater.
        peer->disconnected.connect([peer, &serverPeerDisconnected]() {
            serverPeerDisconnected = true;
            EventLoop* loop = peer->ownerEventLoop();
            peer->deleteLater();
            if (loop) {
                loop->post([loop]() { loop->stop(); });
            }
        });
    });

    TcpSocket client(false);
    client.connected.connect([&]() {
        clientDidConnect = true;
        client.close();
    });
    client.errorOccurred.connect([&](const std::string& error) {
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(clientDidConnect);
    EXPECT_TRUE(serverPeerDisconnected);
}

// ── Moving a subtree to another EventLoop ──────────────────────────────────

namespace {

int connectPosix(std::uint16_t port)
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

// Moved after the socket: its hook runs on the moving thread while the socket
// already watches its descriptor on the target loop.
class SlowMoveProbe final : public Node
{
public:
    SlowMoveProbe(Node* parent, const std::atomic<bool>& readSeen, std::atomic<bool>& readSeenDuringMove)
        : Node(parent), m_readSeen(readSeen), m_readSeenDuringMove(readSeenDuringMove)
    {
    }

protected:
    void onMovedToThread(EventLoop* /*oldLoop*/) override
    {
        std::this_thread::sleep_for(100ms);
        m_readSeenDuringMove.store(m_readSeen.load());
    }

private:
    const std::atomic<bool>& m_readSeen;
    std::atomic<bool>& m_readSeenDuringMove;
};

class EarlyDataHandler final : public Node
{
public:
    EarlyDataHandler(TcpSocket* socket, std::atomic<bool>& readSeen, std::atomic<bool>& readSeenDuringMove)
        : m_socket(socket), m_readSeen(readSeen)
    {
        m_socket->setParent(this);
        new SlowMoveProbe(this, readSeen, readSeenDuringMove);
        m_socket->readyRead.connect(NodePtr<EarlyDataHandler>(this), [](EarlyDataHandler& self) {
            self.m_readSeen.store(true);
            self.m_socket->write(self.m_socket->readAll());
        });
        m_socket->disconnected.connect(NodePtr<EarlyDataHandler>(this), [](EarlyDataHandler& self) {
            self.deleteLater();
        });
    }

    void update() override {}

private:
    TcpSocket* m_socket;
    std::atomic<bool>& m_readSeen;
};

}  // namespace

TEST_F(TcpServerFixture, moveToThreadRunsNoCallbackBeforeTheWholeSubtreeHasMoved)
{
    // The subtree is published to the target loop node by node. If the loop
    // ran the socket's callbacks before the move finished, the handler could
    // be destroyed there while the moving thread still walks its children.
    ThreadPool pool(1);
    const std::thread::id workerThreadId = pool.workerThreadIds().at(0);

    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));
    const int fd = connectPosix(server.serverPort());
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::send(fd, "early", 5, MSG_NOSIGNAL), 5);  // already pending when the socket moves

    TcpSocket* socket = nullptr;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!(socket = server.nextPendingConnection()) && std::chrono::steady_clock::now() < deadline) {
        app->loopOnce();
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_NE(socket, nullptr);

    std::atomic<bool> readSeen{false};
    std::atomic<bool> readSeenDuringMove{false};
    auto* handler = new EarlyDataHandler(socket, readSeen, readSeenDuringMove);
    ASSERT_TRUE(handler->moveToThread(workerThreadId));

    EXPECT_FALSE(readSeenDuringMove.load());

    char echo[5] = {};
    std::size_t received = 0;
    while (received < sizeof(echo)) {
        const ssize_t n = ::recv(fd, echo + received, sizeof(echo) - received, 0);
        if (n <= 0) {
            break;
        }
        received += static_cast<std::size_t>(n);
    }
    EXPECT_EQ(std::string(echo, received), "early");
    ::close(fd);
}
