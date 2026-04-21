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
