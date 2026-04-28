#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/TcpSocket.h>
#include <SNFWebSocket/WebSocket.h>
#include <SNFWebSocket/WebSocketServer.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class WebSocketServerFixture : public ::testing::Test
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

}  // namespace

TEST_F(WebSocketServerFixture, ClientAndServerCanEchoTextMessage)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const std::string payload = "hello websocket";
    std::string clientReceived;
    std::string clientError;
    std::string serverError;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->textMessageReceived.connect([peer](const std::string& message) {
            peer->sendTextMessage(message);
        });
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        stopLoopFrom(server);
    });

    WebSocket client;
    client.connected.connect([&]() {
        client.sendTextMessage(payload);
    });
    client.textMessageReceived.connect([&](const std::string& message) {
        clientReceived = message;
        client.close();
        stopLoopFrom(client);
    });
    client.errorOccurred.connect([&](const std::string& error) {
        clientError = error;
        stopLoopFrom(client);
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(clientError.empty()) << clientError;
    EXPECT_TRUE(serverError.empty()) << serverError;
    EXPECT_EQ(clientReceived, payload);
    EXPECT_EQ(acceptedSockets.size(), 1u);
}

TEST_F(WebSocketServerFixture, SupportsEmptyAndMediumTextMessages)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const std::vector<std::string> messages = {"", std::string(4096, 'm')};
    std::vector<std::string> echoes;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->textMessageReceived.connect([peer](const std::string& message) {
            peer->sendTextMessage(message);
        });
    });

    WebSocket client;
    client.connected.connect([&]() {
        client.sendTextMessage(messages[0]);
    });
    client.textMessageReceived.connect([&](const std::string& message) {
        echoes.push_back(message);
        if (echoes.size() < messages.size()) {
            client.sendTextMessage(messages[echoes.size()]);
        } else {
            client.close();
            stopLoopFrom(client);
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(echoes, messages);
}

TEST_F(WebSocketServerFixture, SupportsBinaryMessages)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const std::vector<std::uint8_t> payload = {0, 1, 2, 3, 4, 255};
    std::vector<std::uint8_t> echo;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->binaryMessageReceived.connect([peer](const std::vector<std::uint8_t>& message) {
            peer->sendBinaryMessage(message);
        });
    });

    WebSocket client;
    client.connected.connect([&]() {
        client.sendBinaryMessage(payload);
    });
    client.binaryMessageReceived.connect([&](const std::vector<std::uint8_t>& message) {
        echo = message;
        client.close();
        stopLoopFrom(client);
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(echo, payload);
}

TEST_F(WebSocketServerFixture, SupportsPingPong)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const std::vector<std::uint8_t> payload = {'p', 'o', 'n', 'g'};
    bool gotPong = false;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
    });

    WebSocket client;
    client.connected.connect([&]() {
        client.ping(payload);
    });
    client.pongReceived.connect([&](const std::vector<std::uint8_t>& message) {
        gotPong = message == payload;
        client.close();
        stopLoopFrom(client);
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(gotPong);
}

TEST_F(WebSocketServerFixture, MultipleClientsGetIndependentConnections)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->textMessageReceived.connect([peer](const std::string& message) {
            peer->sendTextMessage(message);
        });
    });

    WebSocket clientA;
    WebSocket clientB;
    int echoes = 0;

    auto onEcho = [&](const std::string&) {
        ++echoes;
        if (echoes == 2) {
            clientA.close();
            clientB.close();
            stopLoopFrom(server);
        }
    };

    clientA.connected.connect([&]() { clientA.sendTextMessage("a"); });
    clientB.connected.connect([&]() { clientB.sendTextMessage("b"); });
    clientA.textMessageReceived.connect(onEcho);
    clientB.textMessageReceived.connect(onEcho);

    Timer shutdown;
    armShutdown(shutdown, 3s);

    clientA.connectToHost(HostAddress::LocalHost, server.serverPort());
    clientB.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(echoes, 2);
    EXPECT_EQ(acceptedSockets.size(), 2u);
}

TEST_F(WebSocketServerFixture, RejectsPlainTcpHandshake)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    bool serverError = false;
    server.errorOccurred.connect([&](const std::string&) {
        serverError = true;
        stopLoopFrom(server);
    });

    TcpSocket client(false);
    client.connected.connect([&]() {
        client.write("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(serverError);
}
