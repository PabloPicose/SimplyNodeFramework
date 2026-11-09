// Protocol-level tests that drive WebSocketServer with a raw TCP client so the
// exact frames on the wire can be controlled and inspected (RFC 6455).

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/TcpSocket.h>
#include <SNFWebSocket/WebSocket.h>
#include <SNFWebSocket/WebSocketServer.h>
#include <SNFWebSocket/WebSocketFrame.h>
#include <SNFWebSocket/WebSocketHandshake.h>

using namespace snf;
using namespace std::chrono_literals;
using snf::websocket::detail::OpCode;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes rawFrame(bool fin, OpCode opcode, const Bytes& payload, bool masked = true)
{
    Bytes out;
    out.push_back(static_cast<std::uint8_t>((fin ? 0x80u : 0x00u) | static_cast<std::uint8_t>(opcode)));
    const std::uint8_t maskBit = masked ? 0x80u : 0x00u;
    if (payload.size() <= 125) {
        out.push_back(static_cast<std::uint8_t>(maskBit | payload.size()));
    } else if (payload.size() <= 0xFFFFu) {
        out.push_back(static_cast<std::uint8_t>(maskBit | 126u));
        out.push_back(static_cast<std::uint8_t>(payload.size() >> 8u));
        out.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
    } else {
        out.push_back(static_cast<std::uint8_t>(maskBit | 127u));
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>((payload.size() >> static_cast<unsigned>(shift)) & 0xFFu));
        }
    }
    const std::uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    if (masked) {
        out.insert(out.end(), mask, mask + 4);
    }
    for (std::size_t i = 0; i < payload.size(); ++i) {
        out.push_back(masked ? static_cast<std::uint8_t>(payload[i] ^ mask[i % 4]) : payload[i]);
    }
    return out;
}

Bytes text(const std::string& s)
{
    return Bytes(s.begin(), s.end());
}

Bytes closePayload(std::uint16_t code, const Bytes& reason = {})
{
    Bytes out{static_cast<std::uint8_t>(code >> 8u), static_cast<std::uint8_t>(code & 0xFFu)};
    out.insert(out.end(), reason.begin(), reason.end());
    return out;
}

Bytes concat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const auto& p : parts) {
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

// Result of a raw session against a WebSocketServer.
struct SessionResult {
    bool handshakeOk = false;
    std::string handshakeResponse;
    std::vector<snf::websocket::detail::WebSocketFrame> frames;  // frames sent by the server
    std::vector<std::string> serverTexts;                        // messages seen by the server peer
    std::vector<Bytes> serverBinaries;
    std::vector<std::string> serverErrors;                       // errors on the accepted WebSocket
    std::vector<std::string> listenerErrors;                     // errors on WebSocketServer
    bool tcpClosedByServer = false;

    int closeCode() const
    {
        for (const auto& f : frames) {
            if (f.opcode == OpCode::Close) {
                if (f.payload.size() < 2) {
                    return 1005;
                }
                return (f.payload[0] << 8) | f.payload[1];
            }
        }
        return -1;
    }
};

class WebSocketProtocolFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }

    void TearDown() override
    {
        for (WebSocket* socket : acceptedSockets) {
            delete socket;
        }
        acceptedSockets.clear();
        delete app;
        app = nullptr;
    }

    // Opens a raw TCP connection, sends `request` (handshake) immediately
    // followed by `withHandshake`, then `afterHandshake` once the 101 arrives.
    SessionResult run(const Bytes& afterHandshake,
                      const Bytes& withHandshake = {},
                      std::string request = {},
                      std::chrono::milliseconds timeout = 1500ms,
                      bool echo = false)
    {
        SessionResult result;
        WebSocketServer server;
        configureServer(server);
        EXPECT_TRUE(server.listen(HostAddress::LocalHost, 0));

        server.errorOccurred.connect([&](const std::string& e) { result.listenerErrors.push_back(e); });
        server.newConnection.connect([&]() {
            while (WebSocket* peer = server.nextPendingConnection()) {
                acceptedSockets.push_back(peer);
                peer->textMessageReceived.connect([&, peer](const std::string& m) {
                    result.serverTexts.push_back(m);
                    if (echo) {
                        peer->sendTextMessage(m);
                    }
                });
                peer->binaryMessageReceived.connect([&](const Bytes& m) { result.serverBinaries.push_back(m); });
                peer->errorOccurred.connect([&](const std::string& e) { result.serverErrors.push_back(e); });
            }
        });

        const std::string key = websocket::detail::createClientKey();
        if (request.empty()) {
            request = websocket::detail::buildClientHandshakeRequest("127.0.0.1", server.serverPort(), "/", key);
        }

        std::string buffer;
        snf::websocket::detail::WebSocketFrameParser parser;
        TcpSocket client(false);
        client.connected.connect([&]() {
            Bytes first(request.begin(), request.end());
            first.insert(first.end(), withHandshake.begin(), withHandshake.end());
            client.write(first);
        });
        client.readyRead.connect([&]() {
            const ByteArray data = client.readAll();
            if (!result.handshakeOk) {
                buffer += data.toString();
                const auto end = buffer.find("\r\n\r\n");
                if (end == std::string::npos) {
                    return;
                }
                result.handshakeResponse = buffer.substr(0, end + 4);
                std::string err;
                result.handshakeOk =
                    websocket::detail::validateServerHandshakeResponse(result.handshakeResponse, key, err);
                const std::string rest = buffer.substr(end + 4);
                if (!rest.empty()) {
                    parser.feed(reinterpret_cast<const std::uint8_t*>(rest.data()), rest.size());
                }
                if (result.handshakeOk && !afterHandshake.empty()) {
                    client.write(afterHandshake);
                }
            } else {
                parser.feed(data.bytesView());
            }
            while (parser.hasFrame()) {
                result.frames.push_back(parser.nextFrame());
            }
        });
        client.disconnected.connect([&]() {
            result.tcpClosedByServer = true;
            if (EventLoop* loop = client.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        });

        Timer shutdown;
        shutdown.setSingleShot(true);
        shutdown.timeout.connect([&]() {
            if (EventLoop* loop = shutdown.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        });
        shutdown.start(timeout);

        client.connectToHost(HostAddress::LocalHost, server.serverPort());
        app->run();
        client.close();
        return result;
    }

    std::function<void(WebSocketServer&)> configureServer = [](WebSocketServer&) {};
    Application* app = nullptr;
    std::vector<WebSocket*> acceptedSockets;
};

}  // namespace

// ── Data framing ─────────────────────────────────────────────────────────────

TEST_F(WebSocketProtocolFixture, ProcessesFramesSentTogetherWithHandshake)
{
    const auto r = run({}, rawFrame(true, OpCode::Text, text("early")));
    ASSERT_TRUE(r.handshakeOk);
    ASSERT_EQ(r.serverTexts.size(), 1u);
    EXPECT_EQ(r.serverTexts[0], "early");
}

TEST_F(WebSocketProtocolFixture, ReassemblesFragmentedTextWithInterleavedPing)
{
    const auto r = run(concat({rawFrame(false, OpCode::Text, text("frag")),
                               rawFrame(true, OpCode::Ping, text("p")),
                               rawFrame(true, OpCode::Continuation, text("mented"))}));
    ASSERT_TRUE(r.handshakeOk);
    ASSERT_EQ(r.serverTexts.size(), 1u);
    EXPECT_EQ(r.serverTexts[0], "fragmented");
    ASSERT_FALSE(r.frames.empty());
    EXPECT_EQ(r.frames[0].opcode, OpCode::Pong);
}

TEST_F(WebSocketProtocolFixture, NewDataFrameDuringFragmentedMessageFailsWith1002)
{
    const auto r = run(concat({rawFrame(false, OpCode::Text, text("a")),
                               rawFrame(true, OpCode::Text, text("b"))}));
    ASSERT_TRUE(r.handshakeOk);
    EXPECT_TRUE(r.serverTexts.empty());
    EXPECT_EQ(r.closeCode(), 1002);
    EXPECT_TRUE(r.tcpClosedByServer);
}

TEST_F(WebSocketProtocolFixture, UnmaskedClientFrameFailsWith1002)
{
    const auto r = run(rawFrame(true, OpCode::Text, text("x"), false));
    ASSERT_TRUE(r.handshakeOk);
    EXPECT_TRUE(r.serverTexts.empty());
    EXPECT_EQ(r.closeCode(), 1002);
}

TEST_F(WebSocketProtocolFixture, ReservedBitsFailWith1002)
{
    Bytes frame = rawFrame(true, OpCode::Text, text("x"));
    frame[0] |= 0x40u;
    const auto r = run(frame);
    ASSERT_TRUE(r.handshakeOk);
    EXPECT_EQ(r.closeCode(), 1002);
}

// ── UTF-8 ───────────────────────────────────────────────────────────────────

TEST_F(WebSocketProtocolFixture, AcceptsValidMultibyteUtf8)
{
    const std::string s = "kosme \xCE\xBA\xE1\xBD\xB9\xCF\x83\xCE\xBC\xCE\xB5 \xF0\x9F\x98\x80";
    const auto r = run(rawFrame(true, OpCode::Text, text(s)));
    ASSERT_EQ(r.serverTexts.size(), 1u);
    EXPECT_EQ(r.serverTexts[0], s);
}

TEST_F(WebSocketProtocolFixture, AcceptsUtf8CodepointSplitAcrossFragments)
{
    // U+1F600 split in the middle of its 4-byte encoding.
    const auto r = run(concat({rawFrame(false, OpCode::Text, Bytes{0xF0, 0x9F}),
                               rawFrame(true, OpCode::Continuation, Bytes{0x98, 0x80})}));
    ASSERT_EQ(r.serverTexts.size(), 1u);
    EXPECT_EQ(r.serverTexts[0], "\xF0\x9F\x98\x80");
    EXPECT_EQ(r.closeCode(), -1);
}

TEST_F(WebSocketProtocolFixture, InvalidUtf8TextFailsWith1007)
{
    for (const Bytes& bad : {Bytes{0xC0, 0xAF},              // overlong
                             Bytes{0xED, 0xA0, 0x80},        // UTF-16 surrogate
                             Bytes{0xF4, 0x90, 0x80, 0x80},  // > U+10FFFF
                             Bytes{0xFF},
                             Bytes{'a', 0xCE}}) {            // truncated at end of message
        const auto r = run(rawFrame(true, OpCode::Text, bad));
        EXPECT_TRUE(r.serverTexts.empty());
        EXPECT_EQ(r.closeCode(), 1007);
    }
}

TEST_F(WebSocketProtocolFixture, InvalidUtf8InFirstFragmentFailsFast)
{
    // The final fragment never arrives: the error must be detected on the first one.
    const auto r = run(rawFrame(false, OpCode::Text, Bytes{'o', 'k', 0xFF}));
    EXPECT_TRUE(r.serverTexts.empty());
    EXPECT_EQ(r.closeCode(), 1007);
}

TEST_F(WebSocketProtocolFixture, BinaryMessagesAreNotUtf8Validated)
{
    const auto r = run(rawFrame(true, OpCode::Binary, Bytes{0xFF, 0xC0, 0x00}));
    ASSERT_EQ(r.serverBinaries.size(), 1u);
    EXPECT_EQ(r.closeCode(), -1);
}

// ── Close handshake ─────────────────────────────────────────────────────────

TEST_F(WebSocketProtocolFixture, EchoesCloseCodeAndClosesTcp)
{
    const auto r = run(rawFrame(true, OpCode::Close, closePayload(1000, text("bye"))));
    EXPECT_EQ(r.closeCode(), 1000);
    EXPECT_TRUE(r.tcpClosedByServer);
}

TEST_F(WebSocketProtocolFixture, EmptyCloseIsEchoedEmpty)
{
    const auto r = run(rawFrame(true, OpCode::Close, {}));
    EXPECT_EQ(r.closeCode(), 1005);
    EXPECT_TRUE(r.tcpClosedByServer);
}

TEST_F(WebSocketProtocolFixture, AcceptsRegisteredAndPrivateCloseCodes)
{
    for (std::uint16_t code : {1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014,
                               3000, 3999, 4000, 4999}) {
        const auto r = run(rawFrame(true, OpCode::Close, closePayload(code)));
        EXPECT_EQ(r.closeCode(), code) << code;
    }
}

TEST_F(WebSocketProtocolFixture, CloseWithOneBytePayloadFailsWith1002)
{
    const auto r = run(rawFrame(true, OpCode::Close, Bytes{0x03}));
    EXPECT_EQ(r.closeCode(), 1002);
}

TEST_F(WebSocketProtocolFixture, CloseWithInvalidCodeFailsWith1002)
{
    for (std::uint16_t code : {0, 999, 1004, 1005, 1006, 1015, 1016, 1100, 2000, 2999, 5000, 65535}) {
        const auto r = run(rawFrame(true, OpCode::Close, closePayload(code)));
        EXPECT_EQ(r.closeCode(), 1002) << code;
    }
}

TEST_F(WebSocketProtocolFixture, CloseWithInvalidUtf8ReasonFailsWith1007)
{
    const auto r = run(rawFrame(true, OpCode::Close, closePayload(1000, Bytes{0xCE, 0xBA, 0xFF})));
    EXPECT_EQ(r.closeCode(), 1007);
}

TEST_F(WebSocketProtocolFixture, DataAfterCloseIsIgnored)
{
    const auto r = run(concat({rawFrame(true, OpCode::Close, closePayload(1000)),
                               rawFrame(true, OpCode::Text, text("late"))}));
    EXPECT_TRUE(r.serverTexts.empty());
    EXPECT_EQ(r.closeCode(), 1000);
}

TEST_F(WebSocketProtocolFixture, LargeEchoIsFullyFlushedBeforeCloseEcho)
{
    // The server queues a large echo and then a Close echo; closing the TCP
    // socket must not drop the bytes still waiting in the write queue.
    const std::string big(8 * 1024 * 1024, 'z');
    const auto r = run(concat({rawFrame(true, OpCode::Text, text(big)),
                               rawFrame(true, OpCode::Close, closePayload(1000))}),
                       {}, {}, 5000ms, /*echo=*/true);
    ASSERT_EQ(r.frames.size(), 2u);
    EXPECT_EQ(r.frames[0].opcode, OpCode::Text);
    EXPECT_EQ(r.frames[0].payload.size(), big.size());
    EXPECT_EQ(r.closeCode(), 1000);
    EXPECT_TRUE(r.tcpClosedByServer);
}

// ── Handshake ────────────────────────────────────────────────────────────────

TEST_F(WebSocketProtocolFixture, RejectsOversizedHandshakeHeaders)
{
    std::string request = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Filler: " + std::string(64 * 1024, 'a');
    const auto r = run({}, {}, request);
    EXPECT_FALSE(r.handshakeOk);
    EXPECT_FALSE(r.listenerErrors.empty());
    EXPECT_TRUE(r.tcpClosedByServer);
}

TEST_F(WebSocketProtocolFixture, RejectedHandshakeGetsHttp400)
{
    const auto r = run({}, {}, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    EXPECT_FALSE(r.handshakeOk);
    EXPECT_EQ(r.handshakeResponse.rfind("HTTP/1.1 400", 0), 0u) << r.handshakeResponse;
    EXPECT_TRUE(r.tcpClosedByServer);
}

// ── Origin allowlist (protection against cross-site WebSocket hijacking) ────

namespace {

std::string requestWithOrigin(const std::string& origin)
{
    std::string request = "GET / HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    if (!origin.empty()) {
        request += "Origin: " + origin + "\r\n";
    }
    return request + "\r\n";
}

bool got101(const SessionResult& r)
{
    return r.handshakeResponse.rfind("HTTP/1.1 101", 0) == 0;
}

}  // namespace

TEST_F(WebSocketProtocolFixture, AnyOriginIsAcceptedByDefault)
{
    const auto r = run({}, {}, requestWithOrigin("https://evil.example"), 300ms);
    EXPECT_TRUE(got101(r)) << r.handshakeResponse;
}

TEST_F(WebSocketProtocolFixture, AllowedOriginIsAccepted)
{
    configureServer = [](WebSocketServer& s) { s.setAllowedOrigins({"http://127.0.0.1:8080", "http://localhost:8080"}); };
    const auto r = run({}, {}, requestWithOrigin("http://localhost:8080"), 300ms);
    EXPECT_TRUE(got101(r)) << r.handshakeResponse;
    EXPECT_EQ(acceptedSockets.size(), 1u);
}

TEST_F(WebSocketProtocolFixture, OriginComparisonIgnoresCase)
{
    configureServer = [](WebSocketServer& s) { s.setAllowedOrigins({"http://LocalHost:8080"}); };
    const auto r = run({}, {}, requestWithOrigin("HTTP://localhost:8080"), 300ms);
    EXPECT_TRUE(got101(r)) << r.handshakeResponse;
}

TEST_F(WebSocketProtocolFixture, ForeignOriginIsRejectedWith403)
{
    configureServer = [](WebSocketServer& s) { s.setAllowedOrigins({"http://127.0.0.1:8080"}); };
    for (const std::string origin : {"https://evil.example", "http://127.0.0.1:8081", "null",
                                     "http://127.0.0.1:8080.evil.example"}) {
        const auto r = run({}, {}, requestWithOrigin(origin), 300ms);
        EXPECT_EQ(r.handshakeResponse.rfind("HTTP/1.1 403", 0), 0u) << origin << "\n" << r.handshakeResponse;
        EXPECT_TRUE(r.tcpClosedByServer) << origin;
        EXPECT_FALSE(r.listenerErrors.empty()) << origin;
    }
    EXPECT_TRUE(acceptedSockets.empty());
}

TEST_F(WebSocketProtocolFixture, RequestWithoutOriginIsAcceptedWhenAllowlistIsSet)
{
    // Non-browser clients (including snf::WebSocket) do not send Origin, and
    // browsers always do, so a missing header is not a cross-site request.
    configureServer = [](WebSocketServer& s) { s.setAllowedOrigins({"http://127.0.0.1:8080"}); };
    const auto r = run({}, {}, requestWithOrigin(""), 300ms);
    EXPECT_TRUE(got101(r)) << r.handshakeResponse;
}
