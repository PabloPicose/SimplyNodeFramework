#include "SNFWebSocket/WebSocket.h"

#ifndef __EMSCRIPTEN__
#include "SNFNetwork/TcpSocket.h"
#endif

#include "SNFWebSocket/WebSocketBackend.h"

#include <utility>

namespace snf {

WebSocket::WebSocket(Node* parent)
    : Node(parent), m_backend(websocket::detail::createWebSocketBackend(*this))
{
}

#ifndef __EMSCRIPTEN__
WebSocket::WebSocket(std::unique_ptr<TcpSocket> acceptedSocket, Node* parent)
    : Node(parent),
      m_backend(websocket::detail::createWebSocketNativeBackend(*this, std::move(acceptedSocket)))
{
}
#endif

WebSocket::~WebSocket()
{
    close();
}

void WebSocket::connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path)
{
    m_backend->connectToHost(address, port, path);
}

bool WebSocket::sendTextMessage(const std::string& message)
{
    return m_backend->sendTextMessage(message);
}

bool WebSocket::sendBinaryMessage(const std::vector<std::uint8_t>& data)
{
    return m_backend->sendBinaryMessage(data);
}

bool WebSocket::ping(const std::vector<std::uint8_t>& payload)
{
    return m_backend->ping(payload);
}

void WebSocket::close()
{
    if (m_backend) {
        m_backend->close();
    }
}

bool WebSocket::isOpen() const
{
    return m_backend && m_backend->isOpen();
}

bool WebSocket::isValid() const
{
    return m_backend && m_backend->isValid();
}

WebSocketState WebSocket::state() const
{
    return m_backend ? m_backend->state() : WebSocketState::Closed;
}

HostAddress WebSocket::peerAddress() const
{
    return m_backend ? m_backend->peerAddress() : HostAddress();
}

std::uint16_t WebSocket::peerPort() const
{
    return m_backend ? m_backend->peerPort() : 0;
}

void WebSocket::beginServerConnection()
{
    m_backend->beginServerConnection();
}

}  // namespace snf
