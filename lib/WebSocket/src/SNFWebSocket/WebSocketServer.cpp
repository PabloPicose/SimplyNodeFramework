#include "SNFWebSocket/WebSocketServer.h"

#include "SNFNetwork/TcpServer.h"
#include "SNFNetwork/TcpSocket.h"
#include "SNFWebSocket/WebSocket.h"
#include "SNFWebSocket/WebSocketHandshake.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace snf {

namespace {

// Upper bound for the HTTP Upgrade request headers of a pending client.
constexpr std::size_t kMaxHandshakeSize = 16 * 1024;

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

constexpr const char* kForbiddenResponse =
    "HTTP/1.1 403 Forbidden\r\n"
    "Connection: close\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

constexpr const char* kBadRequestResponse =
    "HTTP/1.1 400 Bad Request\r\n"
    "Connection: close\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

}  // namespace

struct WebSocketServer::PendingUpgrade {
    std::unique_ptr<TcpSocket> socket;
    std::string buffer;
    Connection readyReadConnection;
    Connection disconnectedConnection;
    Connection errorConnection;
};

WebSocketServer::WebSocketServer(Node* parent)
    : Node(parent), m_tcpServer(std::make_unique<TcpServer>())
{
    m_tcpServer->newConnection.connect([this]() { handleTcpNewConnection(); });
    m_tcpServer->errorOccurred.connect([this](const std::string& errorMessage) {
        handleTcpServerError(errorMessage);
    });
}

WebSocketServer::~WebSocketServer()
{
    close();
}

bool WebSocketServer::listen(const HostAddress& address, std::uint16_t port)
{
    return m_tcpServer->listen(address, port);
}

void WebSocketServer::close()
{
    if (m_tcpServer) {
        m_tcpServer->close();
    }

    for (auto& pending : m_pendingUpgrades) {
        if (pending) {
            disconnectPendingSignals(*pending);
            if (pending->socket) {
                pending->socket->close();
            }
        }
    }
    m_pendingUpgrades.clear();

    for (auto& connection : m_pendingConnections) {
        if (connection) {
            connection->close();
        }
    }
    m_pendingConnections.clear();
}

bool WebSocketServer::isListening() const
{
    return m_tcpServer && m_tcpServer->isListening();
}

std::uint16_t WebSocketServer::serverPort() const
{
    return m_tcpServer ? m_tcpServer->serverPort() : 0;
}

bool WebSocketServer::hasPendingConnections() const
{
    return !m_pendingConnections.empty();
}

WebSocket* WebSocketServer::nextPendingConnection()
{
    if (m_pendingConnections.empty()) {
        return nullptr;
    }

    auto connection = std::move(m_pendingConnections.front());
    m_pendingConnections.pop_front();
    return connection.release();
}

void WebSocketServer::setAllowedOrigins(std::vector<std::string> origins)
{
    for (auto& origin : origins) {
        origin = toLowerAscii(std::move(origin));
    }
    m_allowedOrigins = std::move(origins);
}

const std::vector<std::string>& WebSocketServer::allowedOrigins() const
{
    return m_allowedOrigins;
}

void WebSocketServer::handleTcpNewConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        std::unique_ptr<TcpSocket> socket(m_tcpServer->nextPendingConnection());
        if (!socket) {
            continue;
        }

        auto pending = std::make_shared<PendingUpgrade>();
        pending->socket = std::move(socket);

        // The slots live inside pending->socket, which PendingUpgrade owns:
        // capture a weak_ptr so they do not keep their own owner alive.
        std::weak_ptr<PendingUpgrade> weak = pending;
        pending->readyReadConnection = pending->socket->readyRead.connect([this, weak]() {
            if (auto locked = weak.lock()) {
                handlePendingReadyRead(locked);
            }
        });
        pending->disconnectedConnection = pending->socket->disconnected.connect([this, weak]() {
            if (auto locked = weak.lock()) {
                handlePendingDisconnected(locked);
            }
        });
        pending->errorConnection = pending->socket->errorOccurred.connect(
            [this, weak](const std::string& errorMessage) {
                if (auto locked = weak.lock()) {
                    failPending(locked, errorMessage);
                }
            });

        m_pendingUpgrades.push_back(pending);
    }
}

void WebSocketServer::handleTcpServerError(const std::string& errorMessage)
{
    errorOccurred.emit(errorMessage);
}

void WebSocketServer::handlePendingReadyRead(const std::shared_ptr<PendingUpgrade>& pending)
{
    if (!pending || !pending->socket) {
        return;
    }

    const ByteArray data = pending->socket->readAll();
    pending->buffer += data.toString();

    const auto headerEnd = pending->buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        if (pending->buffer.size() > kMaxHandshakeSize) {
            pending->socket->write(std::string(kBadRequestResponse));
            failPending(pending, "WebSocket upgrade request exceeds " + std::to_string(kMaxHandshakeSize) +
                                     " bytes");
        }
        return;
    }

    const std::string request = pending->buffer.substr(0, headerEnd + 4);
    std::string initialData = pending->buffer.substr(headerEnd + 4);

    std::string clientKey;
    std::string errorMessage;
    if (!websocket::detail::validateClientHandshakeRequest(request, clientKey, errorMessage)) {
        pending->socket->write(std::string(kBadRequestResponse));
        failPending(pending, errorMessage);
        return;
    }

    if (!m_allowedOrigins.empty()) {
        websocket::detail::HttpMessage message;
        websocket::detail::parseHttpMessage(request, message, errorMessage);
        const auto it = message.headers.find("origin");
        if (it != message.headers.end()) {
            const std::string origin = toLowerAscii(it->second);
            if (std::find(m_allowedOrigins.begin(), m_allowedOrigins.end(), origin) == m_allowedOrigins.end()) {
                pending->socket->write(std::string(kForbiddenResponse));
                failPending(pending, "WebSocket upgrade rejected: origin '" + it->second + "' is not allowed");
                return;
            }
        }
    }

    disconnectPendingSignals(*pending);

    pending->socket->write(websocket::detail::buildServerHandshakeResponse(clientKey));

    std::unique_ptr<WebSocket> webSocket(new WebSocket(std::move(pending->socket)));
    webSocket->beginServerConnection(std::move(initialData));

    removePending(pending);
    queueConnection(std::move(webSocket));
}

void WebSocketServer::handlePendingDisconnected(const std::shared_ptr<PendingUpgrade>& pending)
{
    if (pending) {
        disconnectPendingSignals(*pending);
        deletePendingSocketLater(*pending);
    }
    removePending(pending);
}

void WebSocketServer::failPending(const std::shared_ptr<PendingUpgrade>& pending, const std::string& errorMessage)
{
    errorOccurred.emit(errorMessage);
    if (pending) {
        disconnectPendingSignals(*pending);
        deletePendingSocketLater(*pending);
    }
    removePending(pending);
}

void WebSocketServer::disconnectPendingSignals(PendingUpgrade& pending)
{
    pending.readyReadConnection.disconnect();
    pending.disconnectedConnection.disconnect();
    pending.errorConnection.disconnect();
}

void WebSocketServer::deletePendingSocketLater(PendingUpgrade& pending)
{
    if (! pending.socket) {
        return;
    }

    TcpSocket* socket = pending.socket.release();
    if (socket->bytesToWrite() == 0) {
        socket->close();
        socket->deleteLater();
        return;
    }

    // Let a queued error response (HTTP 400) reach the client first;
    // TcpSocket::close() would discard it.
    // The disconnected slot is the single place that schedules the deletion.
    socket->bytesWritten.connect([socket](std::size_t) {
        if (socket->bytesToWrite() == 0 && socket->state() == TcpSocketState::Connected) {
            socket->close();
        }
    });
    socket->disconnected.connect([socket]() { socket->deleteLater(); });
}

void WebSocketServer::removePending(const std::shared_ptr<PendingUpgrade>& pending)
{
    m_pendingUpgrades.erase(std::remove(m_pendingUpgrades.begin(), m_pendingUpgrades.end(), pending),
                            m_pendingUpgrades.end());
}

void WebSocketServer::queueConnection(std::unique_ptr<WebSocket> socket)
{
    m_pendingConnections.push_back(std::move(socket));
    newConnection.emit();
}

}  // namespace snf
