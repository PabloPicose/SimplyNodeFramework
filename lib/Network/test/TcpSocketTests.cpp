#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>

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
        if (!echoServer->listen("127.0.0.1", 0)) {
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

}  // namespace

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

    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didWrite);
    EXPECT_TRUE(didReadyRead);
    EXPECT_EQ(received, payload);
}

TEST_F(TcpSocketFixture, connectToLocalhostHostName)
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

    socket.connectToHost(HostAddress("localhost"), echoServerPort);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(didConnect);
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

    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
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

    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
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

    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
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

    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
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
    
    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
    app->run();
    
    const std::size_t written = socket.write("test-data");
    
    EXPECT_TRUE(didConnect);
    EXPECT_EQ(written, 0);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
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
    
    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
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
                    socket.connectToHost(HostAddress("127.0.0.1"), port);
                });
            }
        }
    });
    
    socket.errorOccurred.connect([&](const std::string& error) { errorMessage = error; });
    
    Timer shutdown;
    armShutdown(shutdown, 3s);
    
    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
    app->run();
    
    EXPECT_TRUE(errorMessage.empty());
    EXPECT_EQ(connectCount, 2);
    EXPECT_EQ(disconnectCount, 1);
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
    
    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
    app->run();
    
    EXPECT_TRUE(errorMessage.empty());
    EXPECT_EQ(received.size(), payloadSize);
    EXPECT_EQ(received, largePayload);
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
    
    socket.connectToHost(HostAddress("127.0.0.1"), echoServerPort);
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
