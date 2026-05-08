#include "SNFWebSocket/WebSocketServer.h"

#include "SNFNetwork/TcpServer.h"
#include "SNFNetwork/TcpSocket.h"
#include "SNFWebSocket/WebSocket.h"
#include "SNFWebSocket/WebSocketHandshake.h"

#include <algorithm>
#include <utility>

namespace snf {

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
        if (pending && pending->socket) {
            pending->socket->close();
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

void WebSocketServer::handleTcpNewConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        std::unique_ptr<TcpSocket> socket(m_tcpServer->nextPendingConnection());
        if (!socket) {
            continue;
        }

        auto pending = std::make_shared<PendingUpgrade>();
        pending->socket = std::move(socket);

        pending->readyReadConnection = pending->socket->readyRead.connect([this, pending]() {
            handlePendingReadyRead(pending);
        });
        pending->disconnectedConnection = pending->socket->disconnected.connect([this, pending]() {
            handlePendingDisconnected(pending);
        });
        pending->errorConnection = pending->socket->errorOccurred.connect(
            [this, pending](const std::string& errorMessage) { failPending(pending, errorMessage); });

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
        return;
    }

    const std::string request = pending->buffer.substr(0, headerEnd + 4);

    std::string clientKey;
    std::string errorMessage;
    if (!websocket::detail::validateClientHandshakeRequest(request, clientKey, errorMessage)) {
        failPending(pending, errorMessage);
        return;
    }

    disconnectPendingSignals(*pending);

    pending->socket->write(websocket::detail::buildServerHandshakeResponse(clientKey));

    std::unique_ptr<WebSocket> webSocket(new WebSocket(std::move(pending->socket)));
    webSocket->beginServerConnection();

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
    socket->close();
    socket->deleteLater();
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
