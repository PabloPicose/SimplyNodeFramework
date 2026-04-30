#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/ByteArray.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>

#include <sys/socket.h>
#include <unistd.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class TcpSocketFixture : public ::testing::Test
{
public:
    void SetUp() override
    {
        app = new Application(0, nullptr);
        
        // Create embedded echo server on ephemeral port
        echoServer = new TcpServer();
        if (!echoServer->listen(HostAddress::LocalHost, 0)) {
            FAIL() << "Failed to start echo server";
            return;
        }
        echoServerPort = echoServer->serverPort();
        
        // Register echo handler
        echoServer->newConnection.connect([this]() {
            TcpSocket* client = echoServer->nextPendingConnection();
            if (!client) {
                return;
            }
            acceptedSockets.push_back(client);
            
            client->readyRead.connect([client]() {
                const std::vector<std::uint8_t> data = client->readAll();
                if (!data.empty()) {
                    client->write(data);  // Echo back
                }
            });
        });
    }

    void TearDown() override
    {
        if (echoServer) {
            echoServer->close();
            delete echoServer;
            echoServer = nullptr;
        }
        for (TcpSocket* socket : acceptedSockets) {
            socket->close();
            delete socket;
        }
        acceptedSockets.clear();
        
        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
    TcpServer* echoServer = nullptr;
    std::uint16_t echoServerPort = 0;
    std::vector<TcpSocket*> acceptedSockets;
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

bool pumpPendingWorkUntil(Application& application,
                          const std::function<bool()>& predicate,
                          std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (EventLoop* loop = application.getOrCreateCurrentThreadEventLoop()) {
            loop->runPendingWork();
        }

        if (predicate()) {
            return true;
        }

        std::this_thread::sleep_for(1ms);
    }

    if (EventLoop* loop = application.getOrCreateCurrentThreadEventLoop()) {
        loop->runPendingWork();
    }
    return predicate();
}

}  // namespace

TEST(ByteArrayTests, tracksRemainingBytesAfterAdvance)
{
    ByteArray bytes(std::vector<std::uint8_t>{1, 2, 3, 4});

    EXPECT_EQ(bytes.size(), 4u);
    EXPECT_EQ(bytes.offset(), 0u);
    EXPECT_EQ(bytes.remainingSize(), 4u);

    bytes.advance(2);

    ASSERT_NE(bytes.remainingData(), nullptr);
    EXPECT_EQ(bytes.offset(), 2u);
    EXPECT_EQ(bytes.remainingSize(), 2u);
    EXPECT_EQ(std::to_integer<unsigned int>(*bytes.remainingData()), 3u);

    bytes.advance(20);

    EXPECT_TRUE(bytes.fullyConsumed());
    EXPECT_EQ(bytes.remainingSize(), 0u);

    bytes.reset();

    EXPECT_EQ(bytes.offset(), 0u);
    EXPECT_EQ(bytes.remainingSize(), 4u);
}

TEST(ByteArrayTests, constructsFromSupportedInputs)
{
    const std::vector<std::uint8_t> expected{'A', 'B', 'C'};
    const ByteArray::Storage byteStorage{std::byte{'A'}, std::byte{'B'}, std::byte{'C'}};

    const ByteArray fromStorage{ByteArray::Storage(byteStorage)};
    const ByteArray fromVector(expected);
    const ByteArray fromString(std::string("ABC"));
    const ByteArray fromStringView(std::string_view("ABC"));
    const ByteArray fromPointer(expected.data(), expected.size());

    auto expectMatches = [&](const ByteArray& bytes) {
        ASSERT_EQ(bytes.size(), expected.size());
        ASSERT_EQ(bytes.remainingSize(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(std::to_integer<std::uint8_t>(bytes.bytes()[i]), expected[i]);
        }
    };

    expectMatches(fromStorage);
    expectMatches(fromVector);
    expectMatches(fromString);
    expectMatches(fromStringView);
    expectMatches(fromPointer);
}

TEST_F(TcpSocketFixture, connectAndEchoRoundTrip)
{
    TcpSocket socket(false);

    const std::string payload = "snf-echo-payload";

    bool didConnect = false;
    bool didReadyRead = false;
    bool didWrite = false;
    std::string received;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        socket.write(payload);
    });

    socket.bytesWritten.connect([&](std::size_t written) {
        if (written > 0) {
            didWrite = true;
        }
    });

    socket.readyRead.connect([&]() {
        didReadyRead = true;
        const std::vector<std::uint8_t> data = socket.readAll();
        received.append(data.begin(), data.end());
        if (received.size() >= payload.size()) {
            if (EventLoop* loop = socket.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didWrite);
    EXPECT_TRUE(didReadyRead);
    EXPECT_EQ(received, payload);
}

TEST_F(TcpSocketFixture, pendingWorkReceivesTcpEventsFromBackgroundDispatcher)
{
    TcpSocket socket(false);

    const std::thread::id ownerThread = std::this_thread::get_id();
    std::thread::id connectedThread;
    std::thread::id readyReadThread;
    const std::string payload = "snf-background-dispatcher";

    bool didConnect = false;
    bool didReadyRead = false;
    std::string received;
    std::string errorMessage;

    socket.connected.connect([&]() {
        connectedThread = std::this_thread::get_id();
        didConnect = true;
        socket.write(payload);
    });

    socket.readyRead.connect([&]() {
        readyReadThread = std::this_thread::get_id();
        didReadyRead = true;
        const std::vector<std::uint8_t> data = socket.readAll();
        received.append(data.begin(), data.end());
    });

    socket.errorOccurred.connect([&](const std::string& error) { errorMessage = error; });

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);

    const bool completed = pumpPendingWorkUntil(*app,
                                                [&]() {
                                                    return received == payload || ! errorMessage.empty();
                                                },
                                                3s);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didReadyRead);
    EXPECT_EQ(received, payload);
    EXPECT_EQ(connectedThread, ownerThread);
    EXPECT_EQ(readyReadThread, ownerThread);
}

TEST_F(TcpSocketFixture, exposesPeerAddressAndPort)
{
    TcpSocket socket(false);

    bool didConnect = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    ASSERT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    ASSERT_TRUE(didConnect);
    EXPECT_EQ(socket.peerAddress().toString(), HostAddress::LocalHost.toString());
    EXPECT_EQ(socket.peerPort(), echoServerPort);
    ASSERT_FALSE(acceptedSockets.empty());
    EXPECT_FALSE(acceptedSockets.front()->peerAddress().isEmpty());
    EXPECT_TRUE(acceptedSockets.front()->peerAddress().isValid());
    EXPECT_GT(acceptedSockets.front()->peerPort(), 0);
}

TEST_F(TcpSocketFixture, disconnectedSocketHasNoPeerEndpoint)
{
    TcpSocket socket(false);

    EXPECT_TRUE(socket.peerAddress().isEmpty());
    EXPECT_EQ(socket.peerPort(), 0);
}

TEST_F(TcpSocketFixture, connectToLocalhostHostName)
{
    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress("localhost"), 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    TcpSocket socket(false);
    TcpSocket* accepted = nullptr;

    bool didConnect = false;
    std::string errorMessage;

    auto stopIfConnectedAndAccepted = [&]() {
        if (accepted && didConnect) {
            if (EventLoop* loop = server.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    };

    server.newConnection.connect([&]() {
        accepted = server.nextPendingConnection();
        if (! accepted) {
            return;
        }

        stopIfConnectedAndAccepted();
    });

    socket.connected.connect([&]() {
        didConnect = true;
        stopIfConnectedAndAccepted();
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    // CI runners can be temporarily slow resolving localhost.
    armShutdown(shutdown, 5s);

    socket.connectToHost(HostAddress("localhost"), port);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);

    if (accepted) {
        accepted->close();
        delete accepted;
    }
}

TEST_F(TcpSocketFixture, closeEmitsDisconnected)
{
    TcpSocket socket(false);

    bool didConnect = false;
    bool didDisconnect = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        socket.close();
    });

    socket.disconnected.connect([&]() {
        didDisconnect = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didDisconnect);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, defaultIsNonBlockingAndCanChangeMode)
{
    TcpSocket socket;

    EXPECT_FALSE(socket.isBlocking());
    socket.setBlocking(true);
    EXPECT_TRUE(socket.isBlocking());
    socket.setBlocking(false);
    EXPECT_FALSE(socket.isBlocking());
}

TEST_F(TcpSocketFixture, blockingSocketConnectAndCloseSafely)
{
    TcpSocket socket(true);

    bool didConnect = false;
    bool didDisconnect = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        socket.close();
    });

    socket.disconnected.connect([&]() {
        didDisconnect = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(socket.isBlocking());
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didDisconnect);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, blockingConnectToHostFromOtherThreadIsMarshaled)
{
    TcpSocket socket(true);

    bool didConnect = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        socket.close();
    });

    socket.disconnected.connect([&]() {
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    std::thread connector([&]() {
        socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);
    app->run();

    connector.join();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, blockingConnectToHostStringFromOtherThreadIsMarshaled)
{
    TcpSocket socket(true);

    bool didConnect = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        socket.close();
    });

    socket.disconnected.connect([&]() {
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    std::thread connector([&]() {
        socket.connectToHost("localhost", echoServerPort);
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);
    app->run();

    connector.join();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, blockingSetBlockingFromOtherThreadIsApplied)
{
    TcpSocket socket(true);

    bool didConnect = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;

        std::thread worker([&]() { socket.setBlocking(false); });
        worker.join();

        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    socket.connectToHost("localhost", echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_FALSE(socket.isBlocking());
}

TEST_F(TcpSocketFixture, adoptedInvalidDescriptorEmitsErrorAndErrorState)
{
    TcpSocket socket(-1, false);

    std::string errorMessage;
    socket.errorOccurred.connect([&](const std::string& error) { errorMessage = error; });

    // Trigger dispatch of the asynchronously emitted constructor error signal.
    Timer shutdown;
    armShutdown(shutdown, 100ms);
    app->run();

    EXPECT_FALSE(errorMessage.empty());
    EXPECT_EQ(socket.state(), TcpSocketState::Error);
}

TEST_F(TcpSocketFixture, closeFromMultipleThreadsEmitsDisconnectedOnce)
{
    TcpSocket socket(false);

    bool didConnect = false;
    std::atomic<int> disconnectCount{0};
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;

        std::vector<std::thread> workers;
        workers.reserve(4);
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&socket]() {
                for (int j = 0; j < 8; ++j) {
                    socket.close();
                }
            });
        }

        for (std::thread& worker : workers) {
            worker.join();
        }
    });

    socket.disconnected.connect([&]() {
        ++disconnectCount;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_EQ(disconnectCount.load(), 1);
}

TEST_F(TcpSocketFixture, writeAndCloseFromDifferentThreadsDoesNotCrash)
{
    TcpSocket socket(false);

    bool didConnect = false;
    bool hasReceived = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;

        std::thread writer([&socket]() {
            for (int i = 0; i < 32; ++i) {
                socket.write("ping");
            }
        });

        std::thread closer([&socket]() { socket.close(); });

        writer.join();
        closer.join();
    });

    socket.readyRead.connect([&]() {
        hasReceived = true;
        (void) socket.readAll();
    });

    socket.disconnected.connect([&]() {
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(hasReceived || socket.state() == TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, directConnectedSlotCanWriteAndCloseSafely)
{
    TcpSocket socket(false);

    bool didConnect = false;
    std::atomic<int> disconnectCount{0};
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        (void) socket.write("from-direct-slot");
        socket.close();
    });

    socket.disconnected.connect([&]() {
        ++disconnectCount;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_EQ(disconnectCount.load(), 1);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, connectionToInvalidHostEmitsError)
{
    TcpSocket socket(false);
    
    bool didConnect = false;
    std::string errorMessage;
    bool errorEmitted = false;
    
    socket.connected.connect([&]() { didConnect = true; });
    
    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        errorEmitted = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    
    Timer shutdown;
    armShutdown(shutdown, 2s);
    
    socket.connectToHost(HostAddress("invalid-hostname-xyz-nonexistent.local"), 80);
    app->run();
    
    EXPECT_FALSE(didConnect);
    EXPECT_TRUE(errorEmitted);
    EXPECT_FALSE(errorMessage.empty());
    EXPECT_EQ(socket.state(), TcpSocketState::Error);
}

TEST_F(TcpSocketFixture, writeToClosedSocketHandledSafely)
{
    TcpSocket socket(false);
    
    bool didConnect = false;
    
    socket.connected.connect([&]() {
        didConnect = true;
        socket.close();
    });
    
    socket.disconnected.connect([&]() {
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    
    Timer shutdown;
    armShutdown(shutdown, 2s);
    
    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();
    
    const std::size_t written = socket.write("test-data");
    
    EXPECT_TRUE(didConnect);
    EXPECT_EQ(written, 0);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, writeImmediatelyAfterConnectToHostIsDelivered)
{
    TcpSocket socket(false);
    const std::string payload = "queued-before-connected-signal";

    std::vector<std::uint8_t> received;
    std::string errorMessage;

    socket.readyRead.connect([&]() {
        const std::vector<std::uint8_t> data = socket.readAll();
        received.insert(received.end(), data.begin(), data.end());
        if (received.size() >= payload.size()) {
            if (EventLoop* loop = socket.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    EXPECT_EQ(socket.write(payload), payload.size());

    Timer shutdown;
    armShutdown(shutdown, 3s);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_EQ(std::string(received.begin(), received.end()), payload);
}

TEST_F(TcpSocketFixture, closeDuringConnectingState)
{
    TcpSocket socket(false);
    
    bool didConnect = false;
    bool didDisconnect = false;
    std::string errorMessage;
    
    socket.connected.connect([&]() { didConnect = true; });
    
    socket.disconnected.connect([&]() {
        didDisconnect = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    
    socket.errorOccurred.connect([&](const std::string& error) { errorMessage = error; });
    
    Timer shutdown;
    armShutdown(shutdown, 1s);
    
    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    socket.close();
    
    app->run();
    
    EXPECT_TRUE(!didConnect || didDisconnect);
    EXPECT_TRUE(errorMessage.empty());
}

TEST_F(TcpSocketFixture, reconnectAfterDisconnect)
{
    TcpSocket socket(false);
    
    int connectCount = 0;
    int disconnectCount = 0;
    std::string errorMessage;
    std::uint16_t port = echoServerPort;
    
    socket.connected.connect([&]() {
        ++connectCount;
        if (connectCount == 1) {
            socket.close();
        } else if (connectCount == 2) {
            if (EventLoop* loop = socket.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });
    
    socket.disconnected.connect([&]() {
        ++disconnectCount;
        if (disconnectCount == 1) {
            if (EventLoop* loop = socket.ownerEventLoop()) {
                loop->post([&socket, port]() {
                    socket.connectToHost(HostAddress::LocalHost, port);
                });
            }
        }
    });
    
    socket.errorOccurred.connect([&](const std::string& error) { errorMessage = error; });
    
    Timer shutdown;
    armShutdown(shutdown, 3s);
    
    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();
    
    EXPECT_TRUE(errorMessage.empty());
    EXPECT_EQ(connectCount, 2);
    EXPECT_EQ(disconnectCount, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// moveToThread tests for TcpSocket
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TcpSocketFixture, moveToThreadBeforeConnectChangesAffinity)
{
    // Create a worker loop and move an idle socket to it.
    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady   = false;
    bool migrationDone = false;
    std::thread::id workerThreadId;

    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(3s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    TcpSocket* socket = new TcpSocket(false);
    EXPECT_EQ(socket->ownerThreadId(), std::this_thread::get_id());

    // Migrate on the owner thread → synchronous path.
    EXPECT_TRUE(socket->moveToThread(workerThreadId));

    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);

    workerLoop->post([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        migrationDone = true;
        cv.notify_one();
        workerLoop->stop();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return migrationDone; });
    }

    worker.join();

    EXPECT_EQ(socket->ownerThreadId(), workerThreadId);
    EXPECT_EQ(socket->ownerEventLoop(), workerLoop);

    delete socket;
}

TEST_F(TcpSocketFixture, connectedSocketCanBeMigratedAndContinuesIO)
{
    // Establish a connection, migrate the socket to a worker thread and verify
    // that we can still read from it after migration.
    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady   = false;
    bool migrationDone = false;
    bool dataReceived  = false;
    std::thread::id workerThreadId;
    std::thread::id readCallbackThread;

    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(5s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);

    TcpSocket* socket = new TcpSocket(false);
    const std::string payload = "migrate-io";

    socket->connected.connect([&]() {
        // On the main thread: migrate the connected socket to the worker.
        EXPECT_TRUE(socket->moveToThread(workerThreadId));
        // Notify the worker that it may now write (after migration lands).
        workerLoop->post([socket, payload]() {
            socket->write(payload);
        });
    });

    socket->readyRead.connect([&]() {
        readCallbackThread = std::this_thread::get_id();
        dataReceived       = true;
        (void) socket->readAll();

        if (EventLoop* loop = socket->ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket->errorOccurred.connect([&](const std::string& error) {
        ADD_FAILURE() << "Socket error: " << error;
        if (EventLoop* loop = socket->ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 4s);

    socket->connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    // Signal the worker to stop (it may have already stopped from readyRead).
    workerLoop->post([workerLoop]() { workerLoop->stop(); });
    worker.join();

    EXPECT_TRUE(dataReceived);
    // The readyRead callback must have fired on the worker thread.
    EXPECT_EQ(readCallbackThread, workerThreadId);

    delete socket;
}

TEST_F(TcpSocketFixture, migratedSocketCanBeClosedFromNewThread)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady  = false;
    bool closedOnNew  = false;
    std::thread::id workerThreadId;

    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(4s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);

    TcpSocket* socket = new TcpSocket(false);

    socket->connected.connect([&]() {
        EXPECT_TRUE(socket->moveToThread(workerThreadId));
        // Post close on the worker after migration.
        workerLoop->post([&]() {
            socket->close();
            closedOnNew = true;
            workerLoop->stop();
        });
    });

    socket->errorOccurred.connect([&](const std::string& error) {
        ADD_FAILURE() << "Socket error: " << error;
        workerLoop->post([workerLoop]() { workerLoop->stop(); });
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    socket->connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();

    worker.join();

    EXPECT_TRUE(closedOnNew);
    EXPECT_EQ(socket->state(), TcpSocketState::Disconnected);

    delete socket;
}

TEST_F(TcpSocketFixture, largeDataTransferStreaming)
{
    TcpSocket socket(false);
    
    const std::size_t payloadSize = 2 * 1024 * 1024;
    std::vector<std::uint8_t> largePayload;
    largePayload.reserve(payloadSize);
    for (std::size_t i = 0; i < payloadSize; ++i) {
        largePayload.push_back(static_cast<std::uint8_t>(i % 256));
    }
    
    std::vector<std::uint8_t> received;
    std::string errorMessage;
    
    socket.connected.connect([&]() { socket.write(largePayload); });
    
    socket.readyRead.connect([&]() {
        const std::vector<std::uint8_t> data = socket.readAll();
        received.insert(received.end(), data.begin(), data.end());
        
        if (received.size() >= payloadSize) {
            if (EventLoop* loop = socket.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });
    
    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    
    Timer shutdown;
    armShutdown(shutdown, 10s);
    
    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();
    
    EXPECT_TRUE(errorMessage.empty());
    EXPECT_EQ(received.size(), payloadSize);
    EXPECT_EQ(received, largePayload);
}

TEST_F(TcpSocketFixture, queuedByteArraysFlushInFifoOrder)
{
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);

    int sendBufferSize = 4096;
    (void) ::setsockopt(descriptors[0], SOL_SOCKET, SO_SNDBUF, &sendBufferSize, sizeof(sendBufferSize));

    TcpSocket socket(descriptors[0], false);
    descriptors[0] = -1;

    const std::size_t firstSize = 4 * 1024 * 1024;
    const std::size_t secondSize = 128 * 1024;
    const std::size_t thirdSize = 64 * 1024;

    const std::vector<std::uint8_t> first(firstSize, static_cast<std::uint8_t>('A'));
    const std::vector<std::uint8_t> second(secondSize, static_cast<std::uint8_t>('B'));
    const std::vector<std::uint8_t> third(thirdSize, static_cast<std::uint8_t>('C'));

    std::size_t totalBytesWritten = 0;
    socket.bytesWritten.connect([&](std::size_t written) { totalBytesWritten += written; });

    EXPECT_EQ(socket.write(ByteArray(first)), first.size());
    EXPECT_EQ(socket.write(ByteArray(second)), second.size());
    EXPECT_EQ(socket.write(ByteArray(third)), third.size());

    std::vector<std::uint8_t> received;
    received.reserve(firstSize + secondSize + thirdSize);

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (received.size() < firstSize + secondSize + thirdSize && std::chrono::steady_clock::now() < deadline) {
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->runPendingWork();
        }

        std::uint8_t buffer[64 * 1024];
        while (true) {
            const ssize_t readBytes = ::recv(descriptors[1], buffer, sizeof(buffer), MSG_DONTWAIT);
            if (readBytes > 0) {
                received.insert(received.end(), buffer, buffer + readBytes);
                continue;
            }

            if (readBytes == 0) {
                ADD_FAILURE() << "Peer closed before all queued data was read";
                break;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            ADD_FAILURE() << "recv() failed with errno " << errno;
            break;
        }

        if (received.size() < firstSize + secondSize + thirdSize) {
            std::this_thread::sleep_for(1ms);
        }
    }

    ::close(descriptors[1]);

    if (EventLoop* loop = socket.ownerEventLoop()) {
        loop->runPendingWork();
    }

    ASSERT_EQ(received.size(), firstSize + secondSize + thirdSize);
    EXPECT_EQ(totalBytesWritten, firstSize + secondSize + thirdSize);
    EXPECT_TRUE(std::all_of(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(firstSize),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('A'); }));
    EXPECT_TRUE(std::all_of(received.begin() + static_cast<std::ptrdiff_t>(firstSize),
                            received.begin() + static_cast<std::ptrdiff_t>(firstSize + secondSize),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('B'); }));
    EXPECT_TRUE(std::all_of(received.begin() + static_cast<std::ptrdiff_t>(firstSize + secondSize), received.end(),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('C'); }));
}

TEST_F(TcpSocketFixture, byteArrayWriteStartsAtCurrentOffset)
{
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);

    TcpSocket socket(descriptors[0], false);
    descriptors[0] = -1;

    ByteArray bytes(std::string("ABCDE"));
    bytes.advance(2);

    EXPECT_EQ(socket.write(std::move(bytes)), 3u);

    std::uint8_t buffer[16] = {};
    const ssize_t readBytes = ::recv(descriptors[1], buffer, sizeof(buffer), 0);
    ::close(descriptors[1]);

    ASSERT_EQ(readBytes, 3);
    EXPECT_EQ(std::string(buffer, buffer + readBytes), "CDE");
}

TEST_F(TcpSocketFixture, closeClearsPendingByteArrayQueue)
{
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);

    int sendBufferSize = 4096;
    ASSERT_EQ(::setsockopt(descriptors[0], SOL_SOCKET, SO_SNDBUF, &sendBufferSize, sizeof(sendBufferSize)), 0);

    TcpSocket socket(descriptors[0], false);
    descriptors[0] = -1;

    std::size_t totalBytesWritten = 0;
    socket.bytesWritten.connect([&](std::size_t written) { totalBytesWritten += written; });

    const std::size_t firstSize = 4 * 1024 * 1024;
    const std::vector<std::uint8_t> first(firstSize, static_cast<std::uint8_t>('A'));
    const std::vector<std::uint8_t> second(128 * 1024, static_cast<std::uint8_t>('B'));

    EXPECT_EQ(socket.write(ByteArray(first)), first.size());
    EXPECT_EQ(socket.write(ByteArray(second)), second.size());

    socket.close();

    if (EventLoop* loop = socket.ownerEventLoop()) {
        loop->runPendingWork();
    }

    std::vector<std::uint8_t> received;
    std::uint8_t buffer[64 * 1024] = {};
    while (true) {
        const ssize_t readBytes = ::recv(descriptors[1], buffer, sizeof(buffer), MSG_DONTWAIT);
        if (readBytes > 0) {
            received.insert(received.end(), buffer, buffer + readBytes);
            continue;
        }
        if (readBytes == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        ADD_FAILURE() << "recv() failed with errno " << errno;
        break;
    }

    ::close(descriptors[1]);

    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
    EXPECT_EQ(totalBytesWritten, received.size());
    EXPECT_LT(received.size(), first.size() + second.size());
    EXPECT_TRUE(std::all_of(received.begin(), received.end(),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('A'); }));
}

TEST_F(TcpSocketFixture, socketStateConsistency)
{
    TcpSocket socket(false);
    
    std::vector<TcpSocketState> stateSequence;
    bool didConnect = false;
    bool didDisconnect = false;
    
    socket.connected.connect([&]() {
        didConnect = true;
        stateSequence.push_back(socket.state());
        EXPECT_EQ(socket.state(), TcpSocketState::Connected);
        socket.close();
    });
    
    socket.disconnected.connect([&]() {
        didDisconnect = true;
        stateSequence.push_back(socket.state());
        EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    
    Timer shutdown;
    armShutdown(shutdown, 2s);
    
    socket.connectToHost(HostAddress::LocalHost, echoServerPort);
    app->run();
    
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didDisconnect);
    EXPECT_GE(stateSequence.size(), 2u);
    
    bool sawConnected = false;
    bool sawDisconnected = false;
    for (TcpSocketState s : stateSequence) {
        if (s == TcpSocketState::Connected) {
            sawConnected = true;
            EXPECT_FALSE(sawDisconnected);
        }
        if (s == TcpSocketState::Disconnected) {
            sawDisconnected = true;
            EXPECT_TRUE(sawConnected);
        }
    }
    EXPECT_TRUE(sawConnected);
    EXPECT_TRUE(sawDisconnected);
}
