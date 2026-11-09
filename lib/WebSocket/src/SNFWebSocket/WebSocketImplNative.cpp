#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/Timer.h"
#include "SNFNetwork/TcpSocket.h"
#include "SNFWebSocket/WebSocketBackend.h"
#include "SNFWebSocket/WebSocketFrame.h"
#include "SNFWebSocket/WebSocketHandshake.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace snf {

namespace {

using websocket::detail::CloseCode::InvalidPayload;
using websocket::detail::CloseCode::Normal;
using websocket::detail::CloseCode::ProtocolError;

// Upper bound for the HTTP Upgrade response headers.
constexpr std::size_t kMaxHandshakeSize = 16 * 1024;

// How long to wait for the peer to answer a Close frame, or for queued bytes
// to drain, before the TCP connection is dropped anyway.
constexpr std::chrono::milliseconds kCloseTimeout{5000};

std::vector<std::uint8_t> toBytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string toString(const std::vector<std::uint8_t>& data)
{
    return std::string(data.begin(), data.end());
}

std::vector<std::uint8_t> closePayload(std::uint16_t code)
{
    return {static_cast<std::uint8_t>(code >> 8u), static_cast<std::uint8_t>(code & 0xFFu)};
}

bool isWildcardConnectAddress(const HostAddress& address)
{
    return address.toString() == HostAddress::AnyIPv4.toString() ||
           address.toString() == HostAddress::AnyIPv6.toString();
}

}  // namespace

class WebSocketImplNative final : public websocket::detail::WebSocketBackend
{
public:
    explicit WebSocketImplNative(WebSocket& owner)
        : m_owner(owner), m_frameParser(std::make_unique<websocket::detail::WebSocketFrameParser>())
    {
    }

    WebSocketImplNative(WebSocket& owner, std::unique_ptr<TcpSocket> acceptedSocket)
        : m_owner(owner),
          m_socket(std::move(acceptedSocket)),
          m_clientMode(false),
          m_frameParser(std::make_unique<websocket::detail::WebSocketFrameParser>())
    {
        // Reparent the accepted socket to the owner WebSocket so that the
        // EventLoop does not track it as an independent root node.  This
        // prevents the EventLoop destructor from deleting the TcpSocket
        // before the WebSocket's own destructor (which owns it via m_socket),
        // which would cause a double-free.  When m_socket's unique_ptr later
        // deletes the TcpSocket, Node::~Node() removes it from the owner's
        // children list, so no double-deletion occurs.
        owner.addChild(m_socket.get());
    }

    ~WebSocketImplNative() override
    {
        m_drainConnection.disconnect();
        if (m_closeTimer) {
            m_closeTimer->stop();
        }
        if (m_socket) {
            m_socket->close();
        }
    }

    void connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path) override;
    bool sendTextMessage(const std::string& message) override;
    bool sendBinaryMessage(const std::vector<std::uint8_t>& data) override;
    bool ping(const std::vector<std::uint8_t>& payload) override;
    void close() override;
    bool isOpen() const override;
    bool isValid() const override;
    WebSocketState state() const override;
    HostAddress peerAddress() const override;
    std::uint16_t peerPort() const override;
    void beginServerConnection(std::string initialData) override;

private:
    void attachSocketSignals();
    void handleTcpConnected();
    void handleTcpReadyRead();
    void handleTcpDisconnected();
    void handleTcpError(const std::string& errorMessage);

    void processHandshakeBytes(Span<const std::byte> data);
    void processFrameBytes(Span<const std::byte> data);
    void processFrame(const websocket::detail::WebSocketFrame& frame);
    void processDataFrame(const websocket::detail::WebSocketFrame& frame);
    void processCloseFrame(const websocket::detail::WebSocketFrame& frame);

    bool sendFrame(websocket::detail::OpCode opcode,
                   const std::vector<std::uint8_t>& payload,
                   bool requireOpen = true);

    /** @brief Transport-level failure: report it and drop the TCP connection. */
    void fail(std::string errorMessage);
    /** @brief Protocol violation by the peer: send Close(@p code), then drop TCP. */
    void failConnection(std::uint16_t code, std::string errorMessage);
    /** @brief Closes TCP once every queued byte (e.g. a Close frame) has been sent. */
    void closeTcpWhenFlushed();
    void armCloseTimer();
    void resetCloseState();

    void setState(WebSocketState state);
    bool shouldMaskOutgoingFrames() const;
    bool fragmentInProgress() const;

private:
    WebSocket& m_owner;
    std::unique_ptr<TcpSocket> m_socket;
    WebSocketState m_state = WebSocketState::Closed;
    bool m_clientMode = true;
    bool m_handshakeComplete = false;
    bool m_closeFrameSent = false;
    bool m_closeFrameReceived = false;
    std::string m_handshakeBuffer;
    std::string m_clientKey;
    std::string m_host;
    std::uint16_t m_port = 0;
    std::string m_path = "/";
    std::uint8_t m_fragmentOpcode = 0;
    std::vector<std::uint8_t> m_fragmentPayload;
    websocket::detail::Utf8Validator m_utf8;
    std::string m_inboundPrefix;  // bytes received together with the server handshake
    std::unique_ptr<websocket::detail::WebSocketFrameParser> m_frameParser;
    std::unique_ptr<Timer> m_closeTimer;
    Connection m_drainConnection;
};

void WebSocketImplNative::connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path)
{
    close();
    resetCloseState();

    if (isWildcardConnectAddress(address)) {
        fail("WebSocket client cannot connect to wildcard address '" + address.toString() +
             "'. Use a concrete peer address such as 127.0.0.1.");
        return;
    }

    m_socket = std::make_unique<TcpSocket>(false);
    m_clientMode = true;
    m_handshakeComplete = false;
    m_handshakeBuffer.clear();
    m_inboundPrefix.clear();
    m_clientKey = websocket::detail::createClientKey();
    m_host = address.host();
    m_port = port;
    m_path = path.empty() ? "/" : path;
    m_frameParser->reset();
    m_fragmentPayload.clear();
    m_fragmentOpcode = static_cast<std::uint8_t>(websocket::detail::OpCode::Continuation);
    m_utf8.reset();

    attachSocketSignals();
    setState(WebSocketState::Connecting);
    m_socket->connectToHost(address, port);
}

bool WebSocketImplNative::sendTextMessage(const std::string& message)
{
    return sendFrame(websocket::detail::OpCode::Text, toBytes(message));
}

bool WebSocketImplNative::sendBinaryMessage(const std::vector<std::uint8_t>& data)
{
    return sendFrame(websocket::detail::OpCode::Binary, data);
}

bool WebSocketImplNative::ping(const std::vector<std::uint8_t>& payload)
{
    if (payload.size() > 125) {
        // Caller error, not a connection failure: refuse the ping only.
        return false;
    }
    return sendFrame(websocket::detail::OpCode::Ping, payload);
}

void WebSocketImplNative::close()
{
    if (m_socket && m_state == WebSocketState::Open && !m_closeFrameSent) {
        sendFrame(websocket::detail::OpCode::Close, closePayload(Normal), false);
        m_closeFrameSent = true;
        setState(WebSocketState::Closing);
        // RFC 6455 §7.1.2: after initiating the close handshake the TCP
        // connection stays open until the peer echoes the Close frame back
        // (handled in processCloseFrame()). The timer bounds that wait.
        armCloseTimer();
        return;
    }

    if (m_state == WebSocketState::Closing) {
        // Close handshake already in progress; let it finish or time out.
        return;
    }

    // Closed, Error or Connecting: drop the TCP connection immediately.
    m_drainConnection.disconnect();
    if (m_socket) {
        m_socket->close();
    }
}

bool WebSocketImplNative::isOpen() const
{
    return m_state == WebSocketState::Open;
}

bool WebSocketImplNative::isValid() const
{
    return m_state != WebSocketState::Error;
}

WebSocketState WebSocketImplNative::state() const
{
    return m_state;
}

HostAddress WebSocketImplNative::peerAddress() const
{
    if (m_socket) {
        const HostAddress tcpPeer = m_socket->peerAddress();
        if (! tcpPeer.isEmpty()) {
            return tcpPeer;
        }
    }

    return HostAddress(m_host);
}

std::uint16_t WebSocketImplNative::peerPort() const
{
    if (m_socket) {
        const std::uint16_t tcpPeerPort = m_socket->peerPort();
        if (tcpPeerPort != 0) {
            return tcpPeerPort;
        }
    }

    return m_port;
}

void WebSocketImplNative::beginServerConnection(std::string initialData)
{
    if (!m_socket) {
        fail("WebSocket server connection has no TCP socket");
        return;
    }

    m_clientMode = false;
    m_handshakeComplete = true;
    resetCloseState();
    m_frameParser->reset();
    m_utf8.reset();
    m_fragmentOpcode = static_cast<std::uint8_t>(websocket::detail::OpCode::Continuation);
    attachSocketSignals();
    setState(WebSocketState::Open);

    if (initialData.empty()) {
        return;
    }

    // Frames that arrived in the same TCP read as the Upgrade request. They
    // are processed on the next loop iteration so the application can first
    // connect its slots from WebSocketServer::newConnection. Any later
    // readyRead consumes m_inboundPrefix first, so ordering is preserved.
    m_inboundPrefix = std::move(initialData);
    if (Executor* executor = m_owner.ownerExecutor()) {
        executor->post([guard = NodePtr<WebSocket>(&m_owner), this]() {
            if (guard) {
                handleTcpReadyRead();
            }
        });
    }
}

void WebSocketImplNative::attachSocketSignals()
{
    if (!m_socket) {
        return;
    }

    m_socket->connected.connect([this]() { handleTcpConnected(); });
    m_socket->readyRead.connect([this]() { handleTcpReadyRead(); });
    m_socket->disconnected.connect([this]() { handleTcpDisconnected(); });
    m_socket->errorOccurred.connect([this](const std::string& errorMessage) { handleTcpError(errorMessage); });
}

void WebSocketImplNative::handleTcpConnected()
{
    if (!m_socket || !m_clientMode) {
        return;
    }

    const std::string request =
        websocket::detail::buildClientHandshakeRequest(m_host, m_port, m_path, m_clientKey);
    m_socket->write(request);
}

void WebSocketImplNative::handleTcpReadyRead()
{
    if (!m_socket) {
        return;
    }

    ByteArray data = m_socket->readAll();
    if (!m_inboundPrefix.empty()) {
        ByteArray combined(m_inboundPrefix);
        m_inboundPrefix.clear();
        combined.append(data.bytesView());
        data = std::move(combined);
    }
    if (data.empty()) {
        return;
    }

    if (!m_handshakeComplete) {
        processHandshakeBytes(data.bytesView());
    } else {
        processFrameBytes(data.bytesView());
    }
}

void WebSocketImplNative::handleTcpDisconnected()
{
    m_drainConnection.disconnect();
    if (m_closeTimer) {
        m_closeTimer->stop();
    }

    if (m_state == WebSocketState::Connecting && !m_handshakeComplete) {
        setState(WebSocketState::Error);
        m_owner.errorOccurred.emit("WebSocket connection closed before the handshake completed");
        return;
    }

    const bool wasActive = m_state == WebSocketState::Connecting ||
                           m_state == WebSocketState::Open ||
                           m_state == WebSocketState::Closing;
    if (m_state != WebSocketState::Error) {
        setState(WebSocketState::Closed);
    }
    if (wasActive) {
        m_owner.disconnected.emit();
    }
}

void WebSocketImplNative::handleTcpError(const std::string& errorMessage)
{
    if (m_state == WebSocketState::Closing || m_state == WebSocketState::Closed) {
        // The close handshake already ran; a reset at this point is noise.
        return;
    }
    fail(errorMessage);
}

void WebSocketImplNative::processHandshakeBytes(Span<const std::byte> data)
{
    m_handshakeBuffer.append(reinterpret_cast<const char*>(data.data()), data.size());

    const auto headerEnd = m_handshakeBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        if (m_handshakeBuffer.size() > kMaxHandshakeSize) {
            fail("WebSocket handshake response exceeds " + std::to_string(kMaxHandshakeSize) + " bytes");
        }
        return;
    }

    const std::string response = m_handshakeBuffer.substr(0, headerEnd + 4);
    const std::string remaining = m_handshakeBuffer.substr(headerEnd + 4);
    m_handshakeBuffer.clear();

    std::string errorMessage;
    if (!websocket::detail::validateServerHandshakeResponse(response, m_clientKey, errorMessage)) {
        fail(errorMessage);
        return;
    }

    m_handshakeComplete = true;
    setState(WebSocketState::Open);
    m_owner.connected.emit();

    if (!remaining.empty() && m_state == WebSocketState::Open) {
        const ByteArray remainingBytes(remaining);
        processFrameBytes(remainingBytes.bytesView());
    }
}

void WebSocketImplNative::processFrameBytes(Span<const std::byte> data)
{
    if (m_closeFrameReceived || m_state == WebSocketState::Error) {
        // RFC 6455 §5.5.1: nothing may follow the peer's Close frame.
        return;
    }

    m_frameParser->feed(data);

    while (m_frameParser->hasFrame()) {
        processFrame(m_frameParser->nextFrame());
        if (m_closeFrameReceived || m_state == WebSocketState::Error || m_state == WebSocketState::Closed) {
            return;
        }
    }

    // Frames that parsed before the malformed one have already been delivered.
    if (m_frameParser->hasError()) {
        failConnection(ProtocolError, m_frameParser->error());
    }
}

void WebSocketImplNative::processFrame(const websocket::detail::WebSocketFrame& frame)
{
    using websocket::detail::OpCode;

    if (!m_clientMode && !frame.masked) {
        failConnection(ProtocolError, "WebSocket client frames must be masked");
        return;
    }

    if (m_clientMode && frame.masked) {
        failConnection(ProtocolError, "WebSocket server frames must not be masked");
        return;
    }

    switch (frame.opcode) {
    case OpCode::Text:
    case OpCode::Binary:
    case OpCode::Continuation:
        processDataFrame(frame);
        return;

    case OpCode::Ping:
        sendFrame(OpCode::Pong, frame.payload);
        return;

    case OpCode::Pong:
        m_owner.pongReceived.emit(frame.payload);
        return;

    case OpCode::Close:
        processCloseFrame(frame);
        return;
    }
}

bool WebSocketImplNative::fragmentInProgress() const
{
    return m_fragmentOpcode != static_cast<std::uint8_t>(websocket::detail::OpCode::Continuation);
}

void WebSocketImplNative::processDataFrame(const websocket::detail::WebSocketFrame& frame)
{
    using websocket::detail::OpCode;

    OpCode messageOpcode = frame.opcode;
    if (frame.opcode == OpCode::Continuation) {
        if (!fragmentInProgress()) {
            failConnection(ProtocolError, "Unexpected WebSocket continuation frame");
            return;
        }
        messageOpcode = static_cast<OpCode>(m_fragmentOpcode);
    } else {
        if (fragmentInProgress()) {
            failConnection(ProtocolError, "New WebSocket data frame while a fragmented message is in progress");
            return;
        }
        m_utf8.reset();
    }

    // Text is validated incrementally so an invalid fragment fails fast.
    if (messageOpcode == OpCode::Text) {
        if (!m_utf8.feed(frame.payload) || (frame.fin && !m_utf8.isComplete())) {
            failConnection(InvalidPayload, "WebSocket text message is not valid UTF-8");
            return;
        }
    }

    if (frame.opcode != OpCode::Continuation && frame.fin) {
        // Single-frame message: deliver without copying into the fragment buffer.
        if (messageOpcode == OpCode::Text) {
            m_owner.textMessageReceived.emit(toString(frame.payload));
        } else {
            m_owner.binaryMessageReceived.emit(frame.payload);
        }
        return;
    }

    if (frame.opcode != OpCode::Continuation) {
        m_fragmentOpcode = static_cast<std::uint8_t>(frame.opcode);
        m_fragmentPayload = frame.payload;
    } else {
        m_fragmentPayload.insert(m_fragmentPayload.end(), frame.payload.begin(), frame.payload.end());
    }

    if (!frame.fin) {
        return;
    }

    auto completePayload = std::move(m_fragmentPayload);
    m_fragmentPayload.clear();
    m_fragmentOpcode = static_cast<std::uint8_t>(OpCode::Continuation);
    if (messageOpcode == OpCode::Text) {
        m_owner.textMessageReceived.emit(toString(completePayload));
    } else {
        m_owner.binaryMessageReceived.emit(completePayload);
    }
}

void WebSocketImplNative::processCloseFrame(const websocket::detail::WebSocketFrame& frame)
{
    using websocket::detail::OpCode;

    std::vector<std::uint8_t> reply;
    if (frame.payload.size() == 1) {
        failConnection(ProtocolError, "WebSocket Close frame payload of 1 byte is invalid");
        return;
    }
    if (frame.payload.size() >= 2) {
        const auto code = static_cast<std::uint16_t>((frame.payload[0] << 8u) | frame.payload[1]);
        if (!websocket::detail::isValidCloseCode(code)) {
            failConnection(ProtocolError, "WebSocket Close frame has invalid status code " + std::to_string(code));
            return;
        }
        if (!websocket::detail::isValidUtf8(frame.payload.data() + 2, frame.payload.size() - 2)) {
            failConnection(InvalidPayload, "WebSocket Close reason is not valid UTF-8");
            return;
        }
        reply = closePayload(code);
    }

    m_closeFrameReceived = true;
    if (!m_closeFrameSent) {
        sendFrame(OpCode::Close, reply, false);
        m_closeFrameSent = true;
    }
    setState(WebSocketState::Closing);
    closeTcpWhenFlushed();
}

bool WebSocketImplNative::sendFrame(websocket::detail::OpCode opcode,
                                    const std::vector<std::uint8_t>& payload,
                                    bool requireOpen)
{
    if (!m_socket) {
        return false;
    }
    if (requireOpen && m_state != WebSocketState::Open) {
        return false;
    }

    const auto encoded = websocket::detail::encodeFrame(opcode, payload, shouldMaskOutgoingFrames());
    return m_socket->write(encoded) == encoded.size();
}

void WebSocketImplNative::fail(std::string errorMessage)
{
    if (m_state == WebSocketState::Error) {
        return;
    }
    setState(WebSocketState::Error);
    m_drainConnection.disconnect();
    m_owner.errorOccurred.emit(std::move(errorMessage));
    if (m_socket) {
        m_socket->close();
    }
}

void WebSocketImplNative::failConnection(std::uint16_t code, std::string errorMessage)
{
    if (m_state == WebSocketState::Error) {
        return;
    }

    // RFC 6455 §7.1.7: send a Close frame with the reason, then drop TCP.
    if (m_socket && m_handshakeComplete && !m_closeFrameSent) {
        sendFrame(websocket::detail::OpCode::Close, closePayload(code), false);
        m_closeFrameSent = true;
    }
    m_frameParser->reset();

    setState(WebSocketState::Error);
    m_owner.errorOccurred.emit(std::move(errorMessage));
    closeTcpWhenFlushed();
}

void WebSocketImplNative::closeTcpWhenFlushed()
{
    if (!m_socket) {
        return;
    }

    if (m_socket->bytesToWrite() == 0) {
        m_drainConnection.disconnect();
        m_socket->close();
        return;
    }

    // TcpSocket::close() discards unsent bytes, so wait for the queue to drain.
    if (!m_drainConnection.connected()) {
        m_drainConnection = m_socket->bytesWritten.connect([this](std::size_t) {
            if (m_socket && m_socket->bytesToWrite() == 0) {
                m_drainConnection.disconnect();
                m_socket->close();
            }
        });
    }
    armCloseTimer();
}

void WebSocketImplNative::armCloseTimer()
{
    if (!m_closeTimer) {
        m_closeTimer = std::make_unique<Timer>();
        // Child of the owner so that it follows it between threads and is not
        // tracked as a root node; the unique_ptr still controls its lifetime.
        m_owner.addChild(m_closeTimer.get());
        m_closeTimer->setSingleShot(true);
        m_closeTimer->timeout.connect([this]() {
            m_drainConnection.disconnect();
            if (m_socket) {
                m_socket->close();
            }
        });
    }
    m_closeTimer->start(kCloseTimeout);
}

void WebSocketImplNative::resetCloseState()
{
    m_closeFrameSent = false;
    m_closeFrameReceived = false;
    m_drainConnection.disconnect();
    if (m_closeTimer) {
        m_closeTimer->stop();
    }
}

void WebSocketImplNative::setState(WebSocketState state)
{
    m_state = state;
}

bool WebSocketImplNative::shouldMaskOutgoingFrames() const
{
    return m_clientMode;
}

namespace websocket::detail {

std::unique_ptr<WebSocketBackend> createWebSocketBackend(WebSocket& owner)
{
    return std::make_unique<WebSocketImplNative>(owner);
}

std::unique_ptr<WebSocketBackend> createWebSocketNativeBackend(WebSocket& owner,
                                                               std::unique_ptr<TcpSocket> acceptedSocket)
{
    return std::make_unique<WebSocketImplNative>(owner, std::move(acceptedSocket));
}

}  // namespace websocket::detail

}  // namespace snf
