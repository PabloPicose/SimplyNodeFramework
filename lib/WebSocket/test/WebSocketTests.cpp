#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>
#include <SNFWebSocket/WebSocket.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class WebSocketClientFixture : public ::testing::Test
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

void stopLoopFrom(Node& node)
{
    if (EventLoop* loop = node.ownerEventLoop()) {
        loop->post([loop]() { loop->stop(); });
    }
}

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

std::uint16_t reserveUnusedPort()
{
    TcpServer server;
    if (!server.listen(HostAddress::LocalHost, 0)) {
        return 9;
    }
    const std::uint16_t port = server.serverPort();
    server.close();
    return port;
}

}  // namespace

TEST_F(WebSocketClientFixture, ConnectsToExternalEchoServerWhenAvailable)
{
    WebSocket socket;

    bool didConnect = false;
    bool gotEcho = false;
    std::string errorMessage;
    const std::string payload = "snf external echo";

    socket.connected.connect([&]() {
        didConnect = true;
        socket.sendTextMessage(payload);
    });

    socket.textMessageReceived.connect([&](const std::string& message) {
        gotEcho = message == payload;
        socket.close();
        stopLoopFrom(socket);
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        stopLoopFrom(socket);
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    socket.connectToHost(HostAddress::LocalHost, 8765);
    app->run();

    if (!didConnect && !errorMessage.empty()) {
        GTEST_SKIP() << "External echo server ws://127.0.0.1:8765 is not available: " << errorMessage;
    }

    EXPECT_TRUE(errorMessage.empty()) << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(gotEcho);
}

TEST_F(WebSocketClientFixture, ReportsErrorWhenPortHasNoServer)
{
    WebSocket socket;

    bool gotError = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        stopLoopFrom(socket);
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        gotError = true;
        errorMessage = error;
        stopLoopFrom(socket);
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    socket.connectToHost(HostAddress::LocalHost, reserveUnusedPort());
    app->run();

    EXPECT_TRUE(gotError);
    EXPECT_FALSE(errorMessage.empty());
    EXPECT_FALSE(socket.isValid());
}

TEST_F(WebSocketClientFixture, ReportsInvalidHandshake)
{
    TcpServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    TcpSocket* accepted = nullptr;
    server.newConnection.connect([&]() {
        accepted = server.nextPendingConnection();
        ASSERT_NE(accepted, nullptr);
        accepted->readyRead.connect([&]() {
            accepted->readAll();
            accepted->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        });
    });

    WebSocket socket;
    bool gotError = false;
    std::string errorMessage;

    socket.errorOccurred.connect([&](const std::string& error) {
        gotError = true;
        errorMessage = error;
        stopLoopFrom(socket);
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    socket.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(gotError);
    EXPECT_FALSE(errorMessage.empty());

    if (accepted) {
        accepted->close();
        delete accepted;
    }
}
