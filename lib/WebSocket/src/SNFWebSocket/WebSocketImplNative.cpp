#include "SNFNetwork/TcpSocket.h"
#include "SNFWebSocket/WebSocketBackend.h"
#include "SNFWebSocket/WebSocketFrame.h"
#include "SNFWebSocket/WebSocketHandshake.h"

#include <algorithm>
#include <utility>

namespace snf {

namespace {

std::vector<std::uint8_t> toBytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string toString(const std::vector<std::uint8_t>& data)
{
    return std::string(data.begin(), data.end());
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
    }

    ~WebSocketImplNative() override
    {
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
    void beginServerConnection() override;

private:
    void attachSocketSignals();
    void handleTcpConnected();
    void handleTcpReadyRead();
    void handleTcpDisconnected();
    void handleTcpError(const std::string& errorMessage);

    void processHandshakeBytes(const std::vector<std::uint8_t>& data);
    void processFrameBytes(const std::vector<std::uint8_t>& data);
    void processFrame(const websocket::detail::WebSocketFrame& frame);

    bool sendFrame(websocket::detail::OpCode opcode,
                   const std::vector<std::uint8_t>& payload,
                   bool requireOpen = true);

    void fail(std::string errorMessage);
    void setState(WebSocketState state);
    bool shouldMaskOutgoingFrames() const;

private:
    WebSocket& m_owner;
    std::unique_ptr<TcpSocket> m_socket;
    WebSocketState m_state = WebSocketState::Closed;
    bool m_clientMode = true;
    bool m_handshakeComplete = false;
    bool m_closeFrameSent = false;
    std::string m_handshakeBuffer;
    std::string m_clientKey;
    std::string m_host;
    std::uint16_t m_port = 0;
    std::string m_path = "/";
    std::uint8_t m_fragmentOpcode = 0;
    std::vector<std::uint8_t> m_fragmentPayload;
    std::unique_ptr<websocket::detail::WebSocketFrameParser> m_frameParser;
};

void WebSocketImplNative::connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path)
{
    close();

    m_socket = std::make_unique<TcpSocket>(false);
    m_clientMode = true;
    m_handshakeComplete = false;
    m_closeFrameSent = false;
    m_handshakeBuffer.clear();
    m_clientKey = websocket::detail::createClientKey();
    m_host = address.host();
    m_port = port;
    m_path = path.empty() ? "/" : path;
    m_frameParser->reset();
    m_fragmentPayload.clear();
    m_fragmentOpcode = static_cast<std::uint8_t>(websocket::detail::OpCode::Continuation);

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
        fail("WebSocket ping payload is too large");
        return false;
    }
    return sendFrame(websocket::detail::OpCode::Ping, payload);
}

void WebSocketImplNative::close()
{
    if (m_socket && m_state == WebSocketState::Open && !m_closeFrameSent) {
        sendFrame(websocket::detail::OpCode::Close, {}, false);
        m_closeFrameSent = true;
        setState(WebSocketState::Closing);
    }

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

void WebSocketImplNative::beginServerConnection()
{
    if (!m_socket) {
        fail("WebSocket server connection has no TCP socket");
        return;
    }

    m_clientMode = false;
    m_handshakeComplete = true;
    m_closeFrameSent = false;
    m_frameParser->reset();
    attachSocketSignals();
    setState(WebSocketState::Open);
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

    const std::vector<std::uint8_t> data = m_socket->readAll();
    if (data.empty()) {
        return;
    }

    if (!m_handshakeComplete) {
        processHandshakeBytes(data);
    } else {
        processFrameBytes(data);
    }
}

void WebSocketImplNative::handleTcpDisconnected()
{
    if (m_state == WebSocketState::Connecting && !m_handshakeComplete) {
        setState(WebSocketState::Error);
        m_owner.errorOccurred.emit("WebSocket connection closed before the handshake completed");
        return;
    }

    const bool wasActive = m_state == WebSocketState::Connecting ||
                           m_state == WebSocketState::Open ||
                           m_state == WebSocketState::Closing;
    setState(WebSocketState::Closed);
    if (wasActive) {
        m_owner.disconnected.emit();
    }
}

void WebSocketImplNative::handleTcpError(const std::string& errorMessage)
{
    fail(errorMessage);
}

void WebSocketImplNative::processHandshakeBytes(const std::vector<std::uint8_t>& data)
{
    m_handshakeBuffer.append(data.begin(), data.end());

    const auto headerEnd = m_handshakeBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
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

    if (!remaining.empty()) {
        processFrameBytes(toBytes(remaining));
    }
}

void WebSocketImplNative::processFrameBytes(const std::vector<std::uint8_t>& data)
{
    m_frameParser->feed(data);
    if (m_frameParser->hasError()) {
        fail(m_frameParser->error());
        return;
    }

    while (m_frameParser->hasFrame()) {
        processFrame(m_frameParser->nextFrame());
        if (m_state == WebSocketState::Error || m_state == WebSocketState::Closed) {
            return;
        }
    }
}

void WebSocketImplNative::processFrame(const websocket::detail::WebSocketFrame& frame)
{
    using websocket::detail::OpCode;

    if (!m_clientMode && !frame.masked) {
        fail("WebSocket client frames must be masked");
        return;
    }

    if (m_clientMode && frame.masked) {
        fail("WebSocket server frames must not be masked");
        return;
    }

    switch (frame.opcode) {
    case OpCode::Text:
    case OpCode::Binary:
        if (frame.fin) {
            if (frame.opcode == OpCode::Text) {
                m_owner.textMessageReceived.emit(toString(frame.payload));
            } else {
                m_owner.binaryMessageReceived.emit(frame.payload);
            }
            return;
        }

        m_fragmentOpcode = static_cast<std::uint8_t>(frame.opcode);
        m_fragmentPayload = frame.payload;
        return;

    case OpCode::Continuation:
        if (m_fragmentOpcode == static_cast<std::uint8_t>(OpCode::Continuation)) {
            fail("Unexpected WebSocket continuation frame");
            return;
        }
        m_fragmentPayload.insert(m_fragmentPayload.end(), frame.payload.begin(), frame.payload.end());
        if (frame.fin) {
            const auto originalOpcode = static_cast<OpCode>(m_fragmentOpcode);
            auto completePayload = std::move(m_fragmentPayload);
            m_fragmentPayload.clear();
            m_fragmentOpcode = static_cast<std::uint8_t>(OpCode::Continuation);
            if (originalOpcode == OpCode::Text) {
                m_owner.textMessageReceived.emit(toString(completePayload));
            } else {
                m_owner.binaryMessageReceived.emit(completePayload);
            }
        }
        return;

    case OpCode::Ping:
        sendFrame(OpCode::Pong, frame.payload);
        return;

    case OpCode::Pong:
        m_owner.pongReceived.emit(frame.payload);
        return;

    case OpCode::Close:
        if (!m_closeFrameSent) {
            sendFrame(OpCode::Close, frame.payload, false);
            m_closeFrameSent = true;
        }
        setState(WebSocketState::Closing);
        if (m_socket) {
            m_socket->close();
        }
        return;
    }
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
    setState(WebSocketState::Error);
    m_owner.errorOccurred.emit(std::move(errorMessage));
    if (m_socket) {
        m_socket->close();
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
