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
#include <SNFWebSocket/WebSocketServer.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class WebSocketClientFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }

    void TearDown() override
    {
        for (WebSocket* socket : acceptedSockets) {
            if (socket) {
                socket->close();
                delete socket;
            }
        }
        acceptedSockets.clear();

        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
    std::vector<WebSocket*> acceptedSockets;
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

TEST_F(WebSocketClientFixture, ConnectsToLocalEchoServer)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    bool serverPeerEndpointOk = false;
    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        serverPeerEndpointOk = peer->peerAddress().isValid() && peer->peerPort() > 0;
        peer->textMessageReceived.connect([peer](const std::string& message) {
            peer->sendTextMessage(message);
        });
    });

    WebSocket socket;

    bool didConnect = false;
    bool gotEcho = false;
    std::string errorMessage;
    const std::string payload = "snf local echo";

    socket.connected.connect([&]() {
        didConnect = true;
        EXPECT_EQ(socket.peerAddress().toString(), HostAddress::LocalHost.toString());
        EXPECT_EQ(socket.peerPort(), server.serverPort());
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

    socket.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << errorMessage;
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(gotEcho);
    EXPECT_TRUE(serverPeerEndpointOk);
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
