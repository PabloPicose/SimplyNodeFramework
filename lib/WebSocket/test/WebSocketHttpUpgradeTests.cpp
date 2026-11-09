// WebSocket served on the HttpServer port: HttpServer::upgrade() hands the
// connection to WebSocketServer::handleUpgrade(), which never calls listen().

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFHttpServer/HttpServer.h>
#include <SNFNetwork/TcpSocket.h>
#include <SNFWebSocket/WebSocket.h>
#include <SNFWebSocket/WebSocketHandshake.h>
#include <SNFWebSocket/WebSocketServer.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

using Bytes = std::vector<std::uint8_t>;

void stopLoopFrom(Node& node)
{
    if (EventLoop* loop = node.ownerEventLoop()) {
        loop->post([loop]() { loop->stop(); });
    }
}

void armShutdown(Timer& timer, std::chrono::milliseconds timeout)
{
    timer.setSingleShot(true);
    timer.timeout.connect([&timer]() { stopLoopFrom(timer); });
    timer.start(timeout);
}

// Masked client text frame (payload <= 125 bytes).
Bytes maskedTextFrame(const std::string& payload)
{
    Bytes out{0x81, static_cast<std::uint8_t>(0x80u | payload.size())};
    const std::uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    out.insert(out.end(), mask, mask + 4);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        out.push_back(static_cast<std::uint8_t>(static_cast<std::uint8_t>(payload[i]) ^ mask[i % 4]));
    }
    return out;
}

std::string upgradeRequest(const std::string& path, const std::string& extraHeaders = {},
                           const std::string& version = "13",
                           const std::string& key = "dGhlIHNhbXBsZSBub25jZQ==")
{
    std::string request = "GET " + path + " HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Version: " + version + "\r\n";
    if (!key.empty()) {
        request += "Sec-WebSocket-Key: " + key + "\r\n";
    }
    return request + extraHeaders + "\r\n";
}

struct RawResult {
    std::string received;
    bool closedByServer = false;
};

class WebSocketHttpUpgradeFixture : public ::testing::Test
{
public:
    void SetUp() override
    {
        app = new Application(0, nullptr);
        http = std::make_unique<HttpServer>();
        ws = std::make_unique<WebSocketServer>();
        http->upgrade("/ws", [this](TcpSocket* socket, std::string data) {
            ws->handleUpgrade(socket, std::move(data));
        });
        http->get("/api/status", [](const HttpRequest&) {
            HttpResponse resp;
            resp.json(R"({"ok":true})");
            return resp;
        });
        ws->errorOccurred.connect([this](const std::string& e) { wsErrors.push_back(e); });
        ASSERT_TRUE(http->listen(HostAddress::LocalHost, 0));
        port = http->serverPort();
    }

    void TearDown() override
    {
        for (WebSocket* socket : acceptedSockets) {
            if (socket) {
                socket->close();
                delete socket;
            }
        }
        acceptedSockets.clear();
        http.reset();
        ws.reset();
        delete app;
        app = nullptr;
    }

    // Accepts every queued connection; `onText` runs for each server-side message.
    void acceptAll(std::function<void(WebSocket*, const std::string&)> onText)
    {
        ws->newConnection.connect([this, onText]() {
            while (WebSocket* peer = ws->nextPendingConnection()) {
                acceptedSockets.push_back(peer);
                peer->textMessageReceived.connect([peer, onText](const std::string& m) { onText(peer, m); });
            }
        });
    }

    // Raw TCP exchange with the HttpServer port: writes every chunk in
    // `chunks` as a separate write on connect and collects the reply until
    // the server closes or the timeout expires.
    RawResult raw(const std::vector<std::string>& chunks, std::chrono::milliseconds timeout = 1000ms)
    {
        RawResult result;
        TcpSocket client(false);
        client.connected.connect([&]() {
            for (const auto& chunk : chunks) {
                client.write(chunk);
            }
        });
        client.readyRead.connect([&]() { result.received += client.readAll().toString(); });
        client.disconnected.connect([&]() {
            result.closedByServer = true;
            stopLoopFrom(client);
        });
        Timer shutdown;
        armShutdown(shutdown, timeout);
        client.connectToHost(HostAddress::LocalHost, port);
        app->run();
        client.close();
        return result;
    }

    Application* app = nullptr;
    std::unique_ptr<HttpServer> http;
    std::unique_ptr<WebSocketServer> ws;
    std::uint16_t port = 0;
    std::vector<WebSocket*> acceptedSockets;
    std::vector<std::string> wsErrors;
};

}  // namespace

TEST_F(WebSocketHttpUpgradeFixture, ExchangesTextBothWaysAndClosesCleanly)
{
    acceptAll([](WebSocket* peer, const std::string& m) { peer->sendTextMessage("echo:" + m); });
    ws->newConnection.connect([this]() {
        // newConnection handlers run in order: acceptAll already took it.
        acceptedSockets.back()->sendTextMessage("welcome");
    });

    std::vector<std::string> clientReceived;
    std::string clientError;
    bool clientConnected = false;
    bool clientDisconnected = false;
    bool peerDisconnected = false;

    WebSocket client;
    client.connected.connect([&]() { clientConnected = true; });
    client.textMessageReceived.connect([&](const std::string& m) {
        clientReceived.push_back(m);
        if (m == "welcome") {
            client.sendTextMessage("hello");
        } else {
            for (WebSocket* peer : acceptedSockets) {
                peer->disconnected.connect([&]() {
                    peerDisconnected = true;
                    if (clientDisconnected) {
                        stopLoopFrom(client);
                    }
                });
            }
            client.close();
        }
    });
    client.disconnected.connect([&]() {
        clientDisconnected = true;
        if (peerDisconnected) {
            stopLoopFrom(client);
        }
    });
    client.errorOccurred.connect([&](const std::string& e) { clientError = e; });

    Timer shutdown;
    armShutdown(shutdown, 2s);
    client.connectToHost(HostAddress::LocalHost, port, "/ws");
    app->run();

    EXPECT_FALSE(ws->isListening());
    EXPECT_TRUE(clientConnected);
    EXPECT_TRUE(clientError.empty()) << clientError;
    EXPECT_TRUE(wsErrors.empty()) << wsErrors.front();
    ASSERT_EQ(clientReceived.size(), 2u);
    EXPECT_EQ(clientReceived[0], "welcome");
    EXPECT_EQ(clientReceived[1], "echo:hello");
    EXPECT_TRUE(clientDisconnected);
    EXPECT_TRUE(peerDisconnected);
    ASSERT_EQ(acceptedSockets.size(), 1u);
    EXPECT_EQ(client.state(), WebSocketState::Closed);
    EXPECT_EQ(acceptedSockets[0]->state(), WebSocketState::Closed);
}

TEST_F(WebSocketHttpUpgradeFixture, PlainGetOnSamePortWorksWhileWebSocketIsOpen)
{
    acceptAll([](WebSocket* peer, const std::string& m) { peer->sendTextMessage(m); });

    std::string httpReply;
    bool httpClosed = false;
    std::string echo;

    WebSocket client;
    TcpSocket httpClient(false);
    httpClient.connected.connect([&]() {
        httpClient.write(std::string("GET /api/status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
    });
    httpClient.readyRead.connect([&]() { httpReply += httpClient.readAll().toString(); });
    httpClient.disconnected.connect([&]() {
        httpClosed = true;
        // The WebSocket is still usable after the HTTP request completed.
        client.sendTextMessage("after-get");
    });

    client.connected.connect([&]() { httpClient.connectToHost(HostAddress::LocalHost, port); });
    client.textMessageReceived.connect([&](const std::string& m) {
        echo = m;
        stopLoopFrom(client);
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);
    client.connectToHost(HostAddress::LocalHost, port, "/ws");
    app->run();

    EXPECT_TRUE(httpClosed);
    EXPECT_EQ(httpReply.rfind("HTTP/1.1 200 OK", 0), 0u) << httpReply;
    EXPECT_NE(httpReply.find(R"({"ok":true})"), std::string::npos) << httpReply;
    EXPECT_EQ(echo, "after-get");
    EXPECT_TRUE(client.isOpen());
    client.close();
}

TEST_F(WebSocketHttpUpgradeFixture, FramesSentRightAfterHandshakeAreNotLost)
{
    std::vector<std::string> serverTexts;
    acceptAll([&](WebSocket*, const std::string& m) { serverTexts.push_back(m); });

    const Bytes f1 = maskedTextFrame("first");
    const Bytes f2 = maskedTextFrame("second");
    const std::string request = upgradeRequest("/ws");

    // Same write as the request, and a request split across two writes
    // followed immediately by more frames.
    const std::vector<std::vector<std::string>> variants = {
        {request + std::string(f1.begin(), f1.end()) + std::string(f2.begin(), f2.end())},
        {request.substr(0, 20), request.substr(20) + std::string(f1.begin(), f1.end()),
         std::string(f2.begin(), f2.end())},
    };
    for (const auto& chunks : variants) {
        serverTexts.clear();
        const auto r = raw(chunks, 500ms);
        EXPECT_EQ(r.received.rfind("HTTP/1.1 101", 0), 0u) << r.received;
        ASSERT_EQ(serverTexts.size(), 2u);
        EXPECT_EQ(serverTexts[0], "first");
        EXPECT_EQ(serverTexts[1], "second");
    }
    EXPECT_TRUE(wsErrors.empty());
}

TEST_F(WebSocketHttpUpgradeFixture, InvalidHandshakesGetHttpErrorAndClose)
{
    ws->setAllowedOrigins({"http://127.0.0.1:8080"});

    struct Case {
        std::string name;
        std::string request;
        std::string status;
    };
    const std::vector<Case> cases = {
        {"missing key", upgradeRequest("/ws", {}, "13", ""), "HTTP/1.1 400"},
        {"version 12", upgradeRequest("/ws", {}, "12"), "HTTP/1.1 400"},
        {"foreign origin", upgradeRequest("/ws", "Origin: https://evil.example\r\n"), "HTTP/1.1 403"},
    };
    for (const auto& c : cases) {
        wsErrors.clear();
        const auto r = raw({c.request});
        EXPECT_EQ(r.received.rfind(c.status, 0), 0u) << c.name << "\n" << r.received;
        EXPECT_TRUE(r.closedByServer) << c.name;
        EXPECT_EQ(wsErrors.size(), 1u) << c.name;
    }
    EXPECT_FALSE(ws->hasPendingConnections());

    // An allowed origin still upgrades.
    const auto ok = raw({upgradeRequest("/ws", "Origin: http://127.0.0.1:8080\r\n")}, 300ms);
    EXPECT_EQ(ok.received.rfind("HTTP/1.1 101", 0), 0u) << ok.received;
    EXPECT_TRUE(ws->hasPendingConnections());
}

TEST_F(WebSocketHttpUpgradeFixture, ManySimultaneousWebSocketsOnSamePort)
{
    constexpr int kClients = 5;
    acceptAll([](WebSocket* peer, const std::string& m) { peer->sendTextMessage("echo:" + m); });

    std::vector<std::unique_ptr<WebSocket>> clients;
    std::vector<std::string> received(kClients);
    int connectedCount = 0;
    int receivedCount = 0;

    for (int i = 0; i < kClients; ++i) {
        clients.push_back(std::make_unique<WebSocket>());
        WebSocket* c = clients.back().get();
        c->connected.connect([&]() {
            // Send only once every client is open, so all are open at once.
            if (++connectedCount == kClients) {
                for (int j = 0; j < kClients; ++j) {
                    clients[j]->sendTextMessage("client-" + std::to_string(j));
                }
            }
        });
        c->textMessageReceived.connect([&, i](const std::string& m) {
            received[i] = m;
            if (++receivedCount == kClients) {
                stopLoopFrom(*clients[0]);
            }
        });
    }

    Timer shutdown;
    armShutdown(shutdown, 3s);
    for (auto& c : clients) {
        c->connectToHost(HostAddress::LocalHost, port, "/ws");
    }
    app->run();

    EXPECT_EQ(connectedCount, kClients);
    EXPECT_EQ(acceptedSockets.size(), static_cast<std::size_t>(kClients));
    for (int i = 0; i < kClients; ++i) {
        EXPECT_EQ(received[i], "echo:client-" + std::to_string(i));
    }
    for (auto& c : clients) {
        c->close();
    }
}

TEST_F(WebSocketHttpUpgradeFixture, ClosingHttpServerKeepsHandedOverWebSockets)
{
    acceptAll([](WebSocket* peer, const std::string& m) { peer->sendTextMessage("echo:" + m); });

    std::string echo;
    bool clientDisconnected = false;
    WebSocket client;
    client.connected.connect([&]() { stopLoopFrom(client); });
    client.textMessageReceived.connect([&](const std::string& m) {
        echo = m;
        stopLoopFrom(client);
    });
    client.disconnected.connect([&]() { clientDisconnected = true; });

    Timer shutdown;
    armShutdown(shutdown, 2s);
    client.connectToHost(HostAddress::LocalHost, port, "/ws");
    app->run();
    ASSERT_TRUE(client.isOpen());

    http->close();
    http.reset();

    armShutdown(shutdown, 2s);
    client.sendTextMessage("still-open");
    app->run();

    EXPECT_EQ(echo, "echo:still-open");
    EXPECT_FALSE(clientDisconnected);
    EXPECT_TRUE(client.isOpen());
    client.close();
}
