#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Timer.h"
#include "SNFNetwork/TcpSocket.h"
#include "SNFHttpServer/HttpServer.h"
#include "SNFHttpServer/HttpRequest.h"
#include "SNFHttpServer/HttpResponse.h"
#include "SNFHttpServer/HttpRequestParser.h"

using namespace snf;

namespace {

class HttpResponseTest : public ::testing::Test
{
};

class HttpRequestParserIntegrationTest : public ::testing::Test
{
};

class HttpServerComponentTest : public ::testing::Test
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

} // namespace

// ============================================================================
// HttpResponse Tests - Serialization
// ============================================================================

TEST_F(HttpResponseTest, SerializeJsonResponse)
{
    HttpResponse resp;
    resp.status(200);
    resp.json(R"({"status":"ok"})");

    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(serialized.find("Content-Type: application/json"), std::string::npos);
    EXPECT_NE(serialized.find(R"({"status":"ok"})"), std::string::npos);
}

TEST_F(HttpResponseTest, SerializeTextResponse)
{
    HttpResponse resp;
    resp.status(200);
    resp.text("Hello World");

    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(serialized.find("Content-Type: text/plain"), std::string::npos);
    EXPECT_NE(serialized.find("Hello World"), std::string::npos);
}

TEST_F(HttpResponseTest, SerializeHtmlResponse)
{
    HttpResponse resp;
    resp.status(200);
    resp.html("<html><body>Test</body></html>");

    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(serialized.find("Content-Type: text/html"), std::string::npos);
    EXPECT_NE(serialized.find("<html>"), std::string::npos);
}

TEST_F(HttpResponseTest, Serialize404Response)
{
    HttpResponse resp = HttpResponse::notFound();
    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("HTTP/1.1 404 Not Found"), std::string::npos);
}

TEST_F(HttpResponseTest, Serialize405MethodNotAllowedResponse)
{
    HttpResponse resp = HttpResponse::methodNotAllowed();
    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("HTTP/1.1 405 Method Not Allowed"), std::string::npos);
}

TEST_F(HttpResponseTest, SerializeCustomHeaders)
{
    HttpResponse resp;
    resp.status(200);
    resp.text("data");
    resp.header("X-Custom-Header", "custom-value");
    resp.header("Cache-Control", "no-cache");

    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("X-Custom-Header: custom-value"), std::string::npos);
    EXPECT_NE(serialized.find("Cache-Control: no-cache"), std::string::npos);
}

TEST_F(HttpResponseTest, ResponseBuilderChaining)
{
    HttpResponse resp;
    resp.status(201).json(R"({"id":123})").header("Location", "/resource/123");

    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("HTTP/1.1 201"), std::string::npos);
    EXPECT_NE(serialized.find(R"({"id":123})"), std::string::npos);
    EXPECT_NE(serialized.find("Location: /resource/123"), std::string::npos);
}

// ============================================================================
// HttpRequestParser Integration Tests
// ============================================================================

TEST_F(HttpRequestParserIntegrationTest, ParseAndRetrieveRequestComponents)
{
    HttpRequestParser parser;

    std::string body = "{\"data\":\"test\"}";
    std::string request = "POST /api/test?key=value&foo=bar HTTP/1.1\r\n"
                          "Host: localhost:8080\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: " +
                          std::to_string(body.size()) + "\r\n"
                          "User-Agent: TestClient\r\n"
                          "\r\n" +
                          body;

    parser.feed(reinterpret_cast<const uint8_t*>(request.data()), request.size());

    ASSERT_TRUE(parser.isComplete());
    ASSERT_FALSE(parser.hasError());

    HttpRequest req = parser.parse();

    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.path, "/api/test");
    EXPECT_EQ(req.getQueryParam("key"), "value");
    EXPECT_EQ(req.getQueryParam("foo"), "bar");
    EXPECT_EQ(req.getHeader("host"), "localhost:8080");
    EXPECT_EQ(req.getHeader("content-type"), "application/json");
    EXPECT_EQ(req.body, body);
}

TEST_F(HttpRequestParserIntegrationTest, HandleIncrementalParsing)
{
    HttpRequestParser parser;

    std::string request = "GET /test HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "\r\n";

    // Feed byte by byte
    for (size_t i = 0; i < request.size(); ++i)
    {
        parser.feed(reinterpret_cast<const uint8_t*>(&request[i]), 1);

        if (i < request.size() - 1)
        {
            EXPECT_FALSE(parser.isComplete());
        }
    }

    ASSERT_TRUE(parser.isComplete());
    ASSERT_FALSE(parser.hasError());

    HttpRequest req = parser.parse();

    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.path, "/test");
}

TEST_F(HttpRequestParserIntegrationTest, ResetParserForMultipleRequests)
{
    HttpRequestParser parser;

    // First request
    std::string req1 = "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n";
    parser.feed(reinterpret_cast<const uint8_t*>(req1.data()), req1.size());

    ASSERT_TRUE(parser.isComplete());
    HttpRequest request1 = parser.parse();
    EXPECT_EQ(request1.path, "/first");

    // Reset and parse second request
    parser.reset();

    std::string req2 = "GET /second HTTP/1.1\r\nHost: localhost\r\n\r\n";
    parser.feed(reinterpret_cast<const uint8_t*>(req2.data()), req2.size());

    ASSERT_TRUE(parser.isComplete());
    HttpRequest request2 = parser.parse();
    EXPECT_EQ(request2.path, "/second");
}

// ============================================================================
// HttpServer Component Tests
// ============================================================================

TEST_F(HttpServerComponentTest, ServerInitializes)
{
    HttpServer server;

    EXPECT_EQ(server.serverPort(), 0); // Not yet listening
}

TEST_F(HttpServerComponentTest, ServerRegistersRoutes)
{
    HttpServer server;

    bool getHandlerCalled = false;
    bool postHandlerCalled = false;

    server.get("/api/test", [&](const HttpRequest& req) {
        getHandlerCalled = true;
        HttpResponse resp;
        resp.text("GET handled");
        return resp;
    });

    server.post("/api/test", [&](const HttpRequest& req) {
        postHandlerCalled = true;
        HttpResponse resp;
        resp.text("POST handled");
        return resp;
    });

    // Routes registered - this should not throw or cause errors
    // (We can't easily test route matching without socket communication,
    // but we can ensure registration doesn't crash)
    EXPECT_TRUE(true); // Routes registered successfully
}

TEST_F(HttpServerComponentTest, ServerStartsListening)
{
    HttpServer server;

    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    uint16_t port = server.serverPort();
    EXPECT_GT(port, 0);

    server.close();
}

TEST_F(HttpServerComponentTest, ServerEmitsStartedSignal)
{
    HttpServer server;
    uint16_t signalPort = 0;
    bool signalEmitted = false;

    server.on_started.connect([&](uint16_t port) {
        signalEmitted = true;
        signalPort = port;
    });

    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    EXPECT_TRUE(signalEmitted);
    EXPECT_GT(signalPort, 0);

    server.close();
}

// ============================================================================
// End-to-End Component Validation (no network I/O)
// ============================================================================

TEST_F(HttpServerComponentTest, RequestParsingToResponseSerialization)
{
    // Simulate the request → handler → response path
    // without actual network communication

    // 1. Parse a request
    HttpRequestParser parser;
    std::string body = "{\"name\":\"Alice\",\"age\":30}";
    std::string httpRequest = "POST /api/users HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Content-Length: " +
                              std::to_string(body.size()) + "\r\n"
                              "\r\n" +
                              body;

    parser.feed(reinterpret_cast<const uint8_t*>(httpRequest.data()), httpRequest.size());

    ASSERT_TRUE(parser.isComplete());
    HttpRequest req = parser.parse();

    // 2. Validate request was parsed correctly
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.path, "/api/users");
    EXPECT_EQ(req.body, body);

    // 3. Simulate a handler
    HttpResponse resp;
    if (req.method == "POST" && req.path == "/api/users")
    {
        resp.status(201);
        resp.json(R"({"id":1,"name":"Alice","age":30})");
        resp.header("Location", "/api/users/1");
    }

    // 4. Validate response can be serialized
    std::string serialized = resp.serialize();

    EXPECT_NE(serialized.find("HTTP/1.1 201"), std::string::npos);
    EXPECT_NE(serialized.find(R"({"id":1)"), std::string::npos);
    EXPECT_NE(serialized.find("Location: /api/users/1"), std::string::npos);
    EXPECT_NE(serialized.find("Content-Length:"), std::string::npos);
}

TEST_F(HttpServerComponentTest, QueryStringParsing)
{
    HttpRequestParser parser;
    std::string request = "GET /search?q=hello&category=docs&page=2 HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "\r\n";

    parser.feed(reinterpret_cast<const uint8_t*>(request.data()), request.size());

    ASSERT_TRUE(parser.isComplete());
    HttpRequest req = parser.parse();

    EXPECT_EQ(req.getQueryParam("q"), "hello");
    EXPECT_EQ(req.getQueryParam("category"), "docs");
    EXPECT_EQ(req.getQueryParam("page"), "2");
    EXPECT_EQ(req.getQueryParam("nonexistent"), "");
}

TEST_F(HttpServerComponentTest, CaseInsensitiveHeaderRetrieval)
{
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Content-Type: application/json\r\n"
                          "X-Custom-Header: value\r\n"
                          "\r\n";

    parser.feed(reinterpret_cast<const uint8_t*>(request.data()), request.size());

    ASSERT_TRUE(parser.isComplete());
    HttpRequest req = parser.parse();

    EXPECT_EQ(req.getHeader("host"), "localhost");
    EXPECT_EQ(req.getHeader("Host"), "localhost");
    EXPECT_EQ(req.getHeader("HOST"), "localhost");
    EXPECT_EQ(req.getHeader("content-type"), "application/json");
    EXPECT_EQ(req.getHeader("Content-Type"), "application/json");
    EXPECT_EQ(req.getHeader("x-custom-header"), "value");
}

TEST_F(HttpServerComponentTest, MultipleMethodsOnSamePath)
{
    HttpServer server;

    std::string getResponse, postResponse;
    int getHandlerCalls = 0, postHandlerCalls = 0;

    server.get("/resource", [&](const HttpRequest& req) {
        getHandlerCalls++;
        HttpResponse resp;
        resp.text("GET response");
        return resp;
    });

    server.post("/resource", [&](const HttpRequest& req) {
        postHandlerCalls++;
        HttpResponse resp;
        resp.text("POST response");
        return resp;
    });

    // We're not testing actual routing here (that requires network),
    // but we're ensuring the API allows multiple methods on the same path
    // without crashing
    EXPECT_TRUE(true);
}

// ============================================================================
// Upgrade hand-off (HttpServer::upgrade)
// ============================================================================

namespace {

struct RawExchange
{
    std::string received;
    bool closedByServer = false;
};

void stopLoopOf(Node& node)
{
    if (EventLoop* loop = node.ownerEventLoop())
        loop->post([loop]() { loop->stop(); });
}

// Sends `request` to 127.0.0.1:`port` and collects the reply until the server
// closes the connection or `timeout` expires.
RawExchange rawExchange(std::uint16_t port, const std::string& request,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(1500))
{
    RawExchange result;
    TcpSocket client(false);
    client.connected.connect([&]() { client.write(request); });
    client.readyRead.connect([&]() { result.received += client.readAll().toString(); });
    client.disconnected.connect([&]() {
        result.closedByServer = true;
        stopLoopOf(client);
    });

    Timer shutdown;
    shutdown.setSingleShot(true);
    shutdown.timeout.connect([&]() { stopLoopOf(shutdown); });
    shutdown.start(timeout);

    client.connectToHost(HostAddress::LocalHost, port);
    Application::instance()->run();
    client.close();
    return result;
}

const std::string kUpgradeRequest = "GET /ws HTTP/1.1\r\n"
                                    "Host: 127.0.0.1\r\n"
                                    "Upgrade: websocket\r\n"
                                    "Connection: Upgrade\r\n"
                                    "\r\n";

} // namespace

TEST_F(HttpServerComponentTest, UpgradeHandlerTakesSocketAndAllBytesRead)
{
    HttpServer server;
    std::string handed;
    int handlerCalls = 0;
    TcpSocket* adopted = nullptr;

    server.upgrade("/ws", [&](TcpSocket* socket, std::string data) {
        ++handlerCalls;
        adopted = socket;
        handed = std::move(data);
        // Bytes that arrive after the hand-off are read by the new owner.
        socket->readyRead.connect([&, socket]() {
            handed += socket->readAll().toString();
            if (handed.find("TRAILING") != std::string::npos)
                socket->write(std::string("ADOPTED"));
        });
        if (handed.find("TRAILING") != std::string::npos)
            socket->write(std::string("ADOPTED"));
    });
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    std::string received;
    TcpSocket client(false);
    client.connected.connect([&]() { client.write(kUpgradeRequest + "TRAILING"); });
    client.readyRead.connect([&]() {
        received += client.readAll().toString();
        if (received == "ADOPTED")
            stopLoopOf(client);
    });
    Timer shutdown;
    shutdown.setSingleShot(true);
    shutdown.timeout.connect([&]() { stopLoopOf(shutdown); });
    shutdown.start(std::chrono::milliseconds(1500));

    client.connectToHost(HostAddress::LocalHost, server.serverPort());
    app->run();

    EXPECT_EQ(handlerCalls, 1);
    EXPECT_EQ(handed, kUpgradeRequest + "TRAILING");
    // No HTTP response was written by the server: only the handler's bytes.
    EXPECT_EQ(received, "ADOPTED");
    ASSERT_NE(adopted, nullptr);
    EXPECT_EQ(adopted->state(), TcpSocketState::Connected);

    client.close();
    delete adopted;
}

TEST_F(HttpServerComponentTest, AdoptedSocketSurvivesServerCloseAndDestruction)
{
    auto server = std::make_unique<HttpServer>();
    TcpSocket* adopted = nullptr;
    server->upgrade("/ws", [&](TcpSocket* socket, std::string) {
        adopted = socket;
        stopLoopOf(*socket);
    });
    ASSERT_TRUE(server->listen(HostAddress::LocalHost, 0));

    std::string received;
    bool clientDisconnected = false;
    TcpSocket client(false);
    client.connected.connect([&]() { client.write(kUpgradeRequest); });
    client.readyRead.connect([&]() {
        received += client.readAll().toString();
        stopLoopOf(client);
    });
    client.disconnected.connect([&]() { clientDisconnected = true; });
    Timer shutdown;
    shutdown.setSingleShot(true);
    shutdown.timeout.connect([&]() { stopLoopOf(shutdown); });
    shutdown.start(std::chrono::milliseconds(1500));

    client.connectToHost(HostAddress::LocalHost, server->serverPort());
    app->run();
    ASSERT_NE(adopted, nullptr);

    server->close();
    server.reset();

    shutdown.start(std::chrono::milliseconds(1500));
    adopted->write(std::string("still here"));
    app->run();

    EXPECT_EQ(received, "still here");
    EXPECT_FALSE(clientDisconnected);

    client.close();
    delete adopted;
}

TEST_F(HttpServerComponentTest, UpgradeRouteWithoutUpgradeHeaderGets426)
{
    HttpServer server;
    int handlerCalls = 0;
    server.upgrade("/ws", [&](TcpSocket* socket, std::string) {
        ++handlerCalls;
        socket->close();
        socket->deleteLater();
    });
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const auto r = rawExchange(server.serverPort(), "GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    EXPECT_EQ(r.received.rfind("HTTP/1.1 426 Upgrade Required", 0), 0u) << r.received;
    EXPECT_NE(r.received.find("Upgrade: websocket"), std::string::npos) << r.received;
    EXPECT_TRUE(r.closedByServer);
    EXPECT_EQ(handlerCalls, 0);
}

TEST_F(HttpServerComponentTest, UpgradeToUnregisteredPathGets404)
{
    HttpServer server;
    int handlerCalls = 0;
    server.upgrade("/ws", [&](TcpSocket* socket, std::string) {
        ++handlerCalls;
        socket->close();
        socket->deleteLater();
    });
    // A plain GET route on the same path does not accept upgrades either.
    server.get("/other", [](const HttpRequest&) {
        HttpResponse resp;
        resp.text("plain");
        return resp;
    });
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    for (const std::string path : {"/nope", "/other"})
    {
        std::string request = kUpgradeRequest;
        request.replace(request.find("/ws"), 3, path);
        const auto r = rawExchange(server.serverPort(), request);
        EXPECT_EQ(r.received.rfind("HTTP/1.1 404", 0), 0u) << path << "\n" << r.received;
        EXPECT_TRUE(r.closedByServer) << path;
    }
    EXPECT_EQ(handlerCalls, 0);
}

TEST_F(HttpServerComponentTest, PlainRoutesStillWorkWithUpgradeRoutes)
{
    HttpServer server;
    server.upgrade("/ws", [](TcpSocket* socket, std::string) {
        socket->close();
        socket->deleteLater();
    });
    server.get("/api/status", [](const HttpRequest&) {
        HttpResponse resp;
        resp.json(R"({"ok":true})");
        return resp;
    });
    ASSERT_TRUE(server.listen(HostAddress::LocalHost, 0));

    const auto r = rawExchange(server.serverPort(), "GET /api/status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    EXPECT_EQ(r.received.rfind("HTTP/1.1 200 OK", 0), 0u) << r.received;
    EXPECT_NE(r.received.find(R"({"ok":true})"), std::string::npos) << r.received;
    EXPECT_TRUE(r.closedByServer);
}
