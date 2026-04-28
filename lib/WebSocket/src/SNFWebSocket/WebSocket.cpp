#include "SNFWebSocket/WebSocket.h"

#include "SNFNetwork/TcpSocket.h"
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

WebSocket::WebSocket(Node* parent)
    : Node(parent), m_frameParser(std::make_unique<websocket::detail::WebSocketFrameParser>())
{
}

WebSocket::WebSocket(std::unique_ptr<TcpSocket> acceptedSocket, Node* parent)
    : Node(parent),
      m_socket(std::move(acceptedSocket)),
      m_clientMode(false),
      m_frameParser(std::make_unique<websocket::detail::WebSocketFrameParser>())
{
}

WebSocket::~WebSocket()
{
    if (m_socket) {
        m_socket->close();
    }
}

void WebSocket::connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path)
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

bool WebSocket::sendTextMessage(const std::string& message)
{
    return sendFrame(websocket::detail::OpCode::Text, toBytes(message));
}

bool WebSocket::sendBinaryMessage(const std::vector<std::uint8_t>& data)
{
    return sendFrame(websocket::detail::OpCode::Binary, data);
}

bool WebSocket::ping(const std::vector<std::uint8_t>& payload)
{
    if (payload.size() > 125) {
        fail("WebSocket ping payload is too large");
        return false;
    }
    return sendFrame(websocket::detail::OpCode::Ping, payload);
}

void WebSocket::close()
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

bool WebSocket::isOpen() const
{
    return m_state == WebSocketState::Open;
}

bool WebSocket::isValid() const
{
    return m_state != WebSocketState::Error;
}

WebSocketState WebSocket::state() const
{
    return m_state;
}

void WebSocket::beginServerConnection()
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

void WebSocket::attachSocketSignals()
{
    if (!m_socket) {
        return;
    }

    m_socket->connected.connect([this]() { handleTcpConnected(); });
    m_socket->readyRead.connect([this]() { handleTcpReadyRead(); });
    m_socket->disconnected.connect([this]() { handleTcpDisconnected(); });
    m_socket->errorOccurred.connect([this](const std::string& errorMessage) { handleTcpError(errorMessage); });
}

void WebSocket::handleTcpConnected()
{
    if (!m_socket || !m_clientMode) {
        return;
    }

    const std::string request =
        websocket::detail::buildClientHandshakeRequest(m_host, m_port, m_path, m_clientKey);
    m_socket->write(request);
}

void WebSocket::handleTcpReadyRead()
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

void WebSocket::handleTcpDisconnected()
{
    if (m_state == WebSocketState::Connecting && !m_handshakeComplete) {
        setState(WebSocketState::Error);
        errorOccurred.emit("WebSocket connection closed before the handshake completed");
        return;
    }

    const bool wasActive = m_state == WebSocketState::Connecting ||
                           m_state == WebSocketState::Open ||
                           m_state == WebSocketState::Closing;
    setState(WebSocketState::Closed);
    if (wasActive) {
        disconnected.emit();
    }
}

void WebSocket::handleTcpError(const std::string& errorMessage)
{
    fail(errorMessage);
}

void WebSocket::processHandshakeBytes(const std::vector<std::uint8_t>& data)
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
    connected.emit();

    if (!remaining.empty()) {
        processFrameBytes(toBytes(remaining));
    }
}

void WebSocket::processFrameBytes(const std::vector<std::uint8_t>& data)
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

void WebSocket::processFrame(const websocket::detail::WebSocketFrame& frame)
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
                textMessageReceived.emit(toString(frame.payload));
            } else {
                binaryMessageReceived.emit(frame.payload);
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
                textMessageReceived.emit(toString(completePayload));
            } else {
                binaryMessageReceived.emit(completePayload);
            }
        }
        return;

    case OpCode::Ping:
        sendFrame(OpCode::Pong, frame.payload);
        return;

    case OpCode::Pong:
        pongReceived.emit(frame.payload);
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

bool WebSocket::sendFrame(websocket::detail::OpCode opcode,
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

void WebSocket::fail(std::string errorMessage)
{
    setState(WebSocketState::Error);
    errorOccurred.emit(std::move(errorMessage));
    if (m_socket) {
        m_socket->close();
    }
}

void WebSocket::setState(WebSocketState state)
{
    m_state = state;
}

bool WebSocket::shouldMaskOutgoingFrames() const
{
    return m_clientMode;
}

}  // namespace snf
