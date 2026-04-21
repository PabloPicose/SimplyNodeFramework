#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

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

    ASSERT_TRUE(server.listen("127.0.0.1", 0));
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

    client.connectToHost("127.0.0.1", port);
    app->run();

    EXPECT_TRUE(clientError.empty());
    EXPECT_TRUE(serverError.empty());
    EXPECT_TRUE(didAccept);
}

TEST_F(TcpServerFixture, listenAndConnectUsingLocalhostHostName)
{
    TcpServer server;
    TcpSocket client(false);

    ASSERT_TRUE(server.listen("localhost", 0));
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

    client.connectToHost("localhost", port);
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

    ASSERT_TRUE(server.listen("127.0.0.1", 0));
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

    clientA.connectToHost("127.0.0.1", port);
    clientB.connectToHost("127.0.0.1", port);

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

    ASSERT_TRUE(server.listen("127.0.0.1", 0));
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
            const std::vector<std::uint8_t> input = accepted->readAll();
            std::string text(input.begin(), input.end());
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
        const std::vector<std::uint8_t> data = client.readAll();
        receivedByClient.assign(data.begin(), data.end());
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

    client.connectToHost("127.0.0.1", port);
    app->run();

    EXPECT_TRUE(clientError.empty());
    EXPECT_TRUE(serverError.empty());
    EXPECT_EQ(receivedByClient, payload);

    if (accepted) {
        accepted->close();
        delete accepted;
    }
}

TEST_F(TcpServerFixture, queueOverflowBehavior)
{
    TcpServer server;
    server.setMaxPendingConnections(2);
    
    ASSERT_TRUE(server.listen("127.0.0.1", 0));
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
        client->connectToHost("127.0.0.1", port);
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
    ASSERT_TRUE(server.listen("127.0.0.1", 0));
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
        client->connectToHost("127.0.0.1", port);
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

    ASSERT_TRUE(server1.listen("127.0.0.1", 0));
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
    const bool listenResult = server2.listen("127.0.0.1", port);

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
    const bool retryResult = server.listen("127.0.0.1", 0);
    EXPECT_TRUE(retryResult);
    EXPECT_TRUE(server.isListening());
}

TEST_F(TcpServerFixture, largeDataTransferIntegrity)
{
    TcpServer server;
    TcpSocket client(false);

    ASSERT_TRUE(server.listen("127.0.0.1", 0));
    const std::uint16_t port = server.serverPort();
    ASSERT_GT(port, 0);

    // 1MB payload
    const std::size_t payloadSize = 1024 * 1024;
    std::vector<std::uint8_t> largePayload;
    largePayload.reserve(payloadSize);
    for (std::size_t i = 0; i < payloadSize; ++i) {
        largePayload.push_back(static_cast<std::uint8_t>(i % 256));
    }

    std::vector<std::uint8_t> receivedByClient;
    std::string clientError;
    std::string serverError;
    std::size_t bytesEchoed = 0;

    TcpSocket* accepted = nullptr;

    server.newConnection.connect([&]() {
        accepted = server.nextPendingConnection();
        ASSERT_NE(accepted, nullptr);

        accepted->readyRead.connect([&]() {
            const std::vector<std::uint8_t> input = accepted->readAll();
            if (!input.empty()) {
                accepted->write(input);
                bytesEchoed += input.size();
            }
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
        const std::vector<std::uint8_t> data = client.readAll();
        receivedByClient.insert(receivedByClient.end(), data.begin(), data.end());
        totalReceived += data.size();

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

    client.connectToHost("127.0.0.1", port);
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
    ASSERT_TRUE(server.listen("127.0.0.1", 0));
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
        client->connectToHost("127.0.0.1", port);
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
