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

TEST_F(WebSocketServerFixture, QueuedBinaryMessagesArriveInOrder)
{
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const std::vector<std::uint8_t> first(2 * 1024 * 1024, static_cast<std::uint8_t>('A'));
    const std::vector<std::uint8_t> second(128 * 1024, static_cast<std::uint8_t>('B'));
    const std::vector<std::uint8_t> third(64 * 1024, static_cast<std::uint8_t>('C'));
    const std::vector<std::vector<std::uint8_t>> expected{first, second, third};

    std::vector<std::vector<std::uint8_t>> echoed;
    std::string clientError;
    std::string serverError;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->binaryMessageReceived.connect([peer](const std::vector<std::uint8_t>& message) {
            peer->sendBinaryMessage(message);
        });
        peer->errorOccurred.connect([&](const std::string& error) {
            serverError = error;
            stopLoopFrom(server);
        });
    });

    server.errorOccurred.connect([&](const std::string& error) {
        serverError = error;
        stopLoopFrom(server);
    });

    WebSocket client;
    client.connected.connect([&]() {
        EXPECT_TRUE(client.sendBinaryMessage(first));
        EXPECT_TRUE(client.sendBinaryMessage(second));
        EXPECT_TRUE(client.sendBinaryMessage(third));
    });
    client.binaryMessageReceived.connect([&](const std::vector<std::uint8_t>& message) {
        echoed.push_back(message);
        if (echoed.size() == expected.size()) {
            client.close();
            stopLoopFrom(client);
        }
    });
    client.errorOccurred.connect([&](const std::string& error) {
        clientError = error;
        stopLoopFrom(client);
    });

    Timer shutdown;
    armShutdown(shutdown, 5s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(clientError.empty()) << clientError;
    EXPECT_TRUE(serverError.empty()) << serverError;
    EXPECT_EQ(echoed, expected);
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

TEST_F(WebSocketServerFixture, DeleteLaterOnDisconnectedDoesNotCrash)
{
    // Regression test: calling deleteLater() inside the disconnected signal handler
    // must not crash EventLoop::run when the deferred deletion is processed.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    bool clientDidConnect = false;
    bool serverPeerDisconnected = false;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        // Do NOT add to acceptedSockets — ownership is transferred via deleteLater.
        peer->disconnected.connect([peer, &serverPeerDisconnected]() {
            serverPeerDisconnected = true;
            EventLoop* loop = peer->ownerEventLoop();
            peer->deleteLater();
            if (loop) {
                loop->post([loop]() { loop->stop(); });
            }
        });
    });

    WebSocket client;
    client.connected.connect([&]() {
        clientDidConnect = true;
        client.close();
    });
    client.errorOccurred.connect([&](const std::string&) {
        stopLoopFrom(client);
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(clientDidConnect);
    EXPECT_TRUE(serverPeerDisconnected);
}

// ─── Server-initiated behaviours ─────────────────────────────────────────────

TEST_F(WebSocketServerFixture, ServerInitiatesClose)
{
    // RFC 6455 §7.1.2: either endpoint may initiate the closing handshake.
    // Server calls close() on the accepted peer; the client must receive
    // disconnected (after echoing the Close frame back).
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        // Initiate the closing handshake immediately after accepting.
        peer->close();
    });

    WebSocket client;
    bool clientDisconnected = false;
    bool clientError = false;

    client.disconnected.connect([&]() {
        clientDisconnected = true;
        stopLoopFrom(client);
    });
    client.errorOccurred.connect([&](const std::string&) {
        clientError = true;
        stopLoopFrom(client);
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_TRUE(clientDisconnected);
    EXPECT_FALSE(clientError);
}

TEST_F(WebSocketServerFixture, ServerSendsMessageBeforeClientDoes)
{
    // Server pushes a message immediately after accepting — client has not
    // sent anything yet.  Models "push notification on connect" pattern.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const std::string greeting = "welcome";

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->sendTextMessage(greeting);
    });

    WebSocket client;
    std::string received;

    client.textMessageReceived.connect([&](const std::string& msg) {
        received = msg;
        client.close();
        stopLoopFrom(client);
    });
    client.errorOccurred.connect([&](const std::string&) { stopLoopFrom(client); });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(received, greeting);
}

TEST_F(WebSocketServerFixture, ServerPingsClientAutoRespondsWithPong)
{
    // RFC 6455 §5.5.3: receiver of a ping MUST respond with pong using the
    // same payload.  The server sends a ping; the pong should arrive back on
    // the server-side peer socket via pongReceived.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const std::vector<std::uint8_t> pingPayload = {'r', 'f', 'c', '6', '4', '5', '5'};
    std::vector<std::uint8_t> receivedPong;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->pongReceived.connect([peer, &receivedPong](const std::vector<std::uint8_t>& payload) {
            receivedPong = payload;
            peer->close();
            stopLoopFrom(*peer);
        });
        peer->ping(pingPayload);
    });

    WebSocket client;
    client.errorOccurred.connect([&](const std::string&) { stopLoopFrom(client); });
    // Keep the client alive until the server stops the loop.
    client.disconnected.connect([&]() { stopLoopFrom(client); });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(receivedPong, pingPayload);
}

// ─── Message-type interleaving ────────────────────────────────────────────────

TEST_F(WebSocketServerFixture, InterleavedTextAndBinaryMessagesEchoedInOrder)
{
    // Mixing text and binary frames on the same connection must not confuse
    // the frame parser or dispatch to the wrong signal.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->textMessageReceived.connect(
            [peer](const std::string& msg) { peer->sendTextMessage(msg); });
        peer->binaryMessageReceived.connect(
            [peer](const std::vector<std::uint8_t>& data) { peer->sendBinaryMessage(data); });
    });

    WebSocket client;
    std::vector<std::string>              receivedText;
    std::vector<std::vector<std::uint8_t>> receivedBinary;
    const std::vector<std::string>              expectedText   = {"alpha", "gamma"};
    const std::vector<std::vector<std::uint8_t>> expectedBinary = {{0x01, 0x02}, {0x03, 0x04}};
    int totalExpected = 4;
    int totalReceived = 0;

    auto checkDone = [&]() {
        if (++totalReceived == totalExpected) {
            client.close();
            stopLoopFrom(client);
        }
    };
    client.textMessageReceived.connect([&](const std::string& msg) {
        receivedText.push_back(msg);
        checkDone();
    });
    client.binaryMessageReceived.connect([&](const std::vector<std::uint8_t>& data) {
        receivedBinary.push_back(data);
        checkDone();
    });
    client.errorOccurred.connect([&](const std::string&) { stopLoopFrom(client); });

    client.connected.connect([&]() {
        client.sendTextMessage(expectedText[0]);
        client.sendBinaryMessage(expectedBinary[0]);
        client.sendTextMessage(expectedText[1]);
        client.sendBinaryMessage(expectedBinary[1]);
    });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(receivedText,   expectedText);
    EXPECT_EQ(receivedBinary, expectedBinary);
}

// ─── Reconnect ───────────────────────────────────────────────────────────────

TEST_F(WebSocketServerFixture, ClientReconnectsAfterCleanDisconnect)
{
    // A client that closes and then calls connectToHost() again must be able
    // to establish a fresh connection.  Models restart-without-restart-app.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->textMessageReceived.connect(
            [peer](const std::string& msg) { peer->sendTextMessage(msg); });
    });

    WebSocket client;
    int connectCount  = 0;
    int echoCount     = 0;
    std::string lastEcho;

    client.connected.connect([&]() {
        ++connectCount;
        client.sendTextMessage("attempt-" + std::to_string(connectCount));
    });
    client.textMessageReceived.connect([&](const std::string& msg) {
        lastEcho = msg;
        ++echoCount;
        client.close();
        if (echoCount < 2) {
            // Reconnect on the next loop iteration.
            if (EventLoop* loop = client.ownerEventLoop()) {
                loop->post([&]() {
                    client.connectToHost(HostAddress::LocalHost, server.serverPort());
                });
            }
        } else {
            stopLoopFrom(client);
        }
    });
    client.errorOccurred.connect([&](const std::string&) { stopLoopFrom(client); });

    Timer shutdown;
    armShutdown(shutdown, 5s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(connectCount, 2);
    EXPECT_EQ(echoCount,    2);
    EXPECT_EQ(lastEcho,     "attempt-2");
}

// ─── Broadcast ───────────────────────────────────────────────────────────────

TEST_F(WebSocketServerFixture, ServerBroadcastsToAllConnectedClients)
{
    // Server waits until N clients are connected, then broadcasts one message
    // to all of them.  Every client must receive the broadcast exactly once.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    constexpr int N = 3;
    const std::string broadcast = "broadcast-payload";
    int receivedCount = 0;

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);

        if (static_cast<int>(acceptedSockets.size()) == N) {
            for (WebSocket* s : acceptedSockets) {
                s->sendTextMessage(broadcast);
            }
        }
    });

    std::array<WebSocket, N> clients;
    for (WebSocket& c : clients) {
        c.textMessageReceived.connect([&](const std::string& msg) {
            EXPECT_EQ(msg, broadcast);
            if (++receivedCount == N) {
                for (WebSocket& s : clients) { s.close(); }
                stopLoopFrom(clients[0]);
            }
        });
        c.errorOccurred.connect([&](const std::string&) { stopLoopFrom(c); });
    }

    Timer shutdown;
    armShutdown(shutdown, 4s);

    for (WebSocket& c : clients) {
        c.connectToHost(HostAddress::LocalHost, server.serverPort());
    }
    app->run();

    EXPECT_EQ(receivedCount, N);
}

// ─── Ping / Pong edge cases ──────────────────────────────────────────────────

TEST_F(WebSocketServerFixture, MultipleRapidPingsAllPongsReceived)
{
    // Autobahn §2.x: consecutive pings must each produce a pong with the
    // matching payload.  This catches implementations that drop queued pings.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
    });

    WebSocket client;
    constexpr int N = 3;
    const std::vector<std::vector<std::uint8_t>> payloads = {{'p', '1'}, {'p', '2'}, {'p', '3'}};
    std::vector<std::vector<std::uint8_t>> pongs;

    client.connected.connect([&]() {
        for (const auto& pl : payloads) {
            client.ping(pl);
        }
    });
    client.pongReceived.connect([&](const std::vector<std::uint8_t>& p) {
        pongs.push_back(p);
        if (static_cast<int>(pongs.size()) == N) {
            client.close();
            stopLoopFrom(client);
        }
    });
    client.errorOccurred.connect([&](const std::string&) { stopLoopFrom(client); });

    Timer shutdown;
    armShutdown(shutdown, 3s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(pongs, payloads);
}

// ─── Large message ───────────────────────────────────────────────────────────

TEST_F(WebSocketServerFixture, LargeTextMessageRoundTrip)
{
    // Autobahn §1.x: text frames up to 64 KiB must round-trip intact.
    WebSocketServer server;
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    server.newConnection.connect([&]() {
        WebSocket* peer = server.nextPendingConnection();
        ASSERT_NE(peer, nullptr);
        acceptedSockets.push_back(peer);
        peer->textMessageReceived.connect(
            [peer](const std::string& msg) { peer->sendTextMessage(msg); });
    });

    const std::string payload(64 * 1024, 'x');
    std::string received;

    WebSocket client;
    client.connected.connect([&]() { client.sendTextMessage(payload); });
    client.textMessageReceived.connect([&](const std::string& msg) {
        received = msg;
        client.close();
        stopLoopFrom(client);
    });
    client.errorOccurred.connect([&](const std::string&) { stopLoopFrom(client); });

    Timer shutdown;
    armShutdown(shutdown, 5s);

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(received.size(), payload.size());
    EXPECT_EQ(received, payload);
}

