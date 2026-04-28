#pragma once

/**
 * @file WebSocketServer.h
 * @brief Non-blocking WebSocket server for local ws:// connections.
 * @ingroup SNFWebSocket
 */

#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include "SNFNetwork/HostAddress.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace snf {

class TcpServer;
class TcpSocket;
class WebSocket;

/**
 * @class WebSocketServer
 * @ingroup SNFWebSocket
 * @brief Listens for `ws://` clients and queues completed WebSocket connections.
 *
 * The server accepts TCP clients, validates the WebSocket Upgrade handshake,
 * sends the server handshake response, and emits `newConnection` once a
 * `WebSocket` is ready to use.
 */
class WebSocketServer : public Node
{
public:
    explicit WebSocketServer(Node* parent = nullptr);
    ~WebSocketServer() override;

    void update() override {}

    bool listen(const HostAddress& address, std::uint16_t port);
    void close();
    bool isListening() const;
    std::uint16_t serverPort() const;

    bool hasPendingConnections() const;
    WebSocket* nextPendingConnection();

    Signal<> newConnection;
    Signal<std::string> errorOccurred;

private:
    struct PendingUpgrade;

    void handleTcpNewConnection();
    void handleTcpServerError(const std::string& errorMessage);
    void handlePendingReadyRead(const std::shared_ptr<PendingUpgrade>& pending);
    void handlePendingDisconnected(const std::shared_ptr<PendingUpgrade>& pending);
    void failPending(const std::shared_ptr<PendingUpgrade>& pending, const std::string& errorMessage);
    void disconnectPendingSignals(PendingUpgrade& pending);
    void deletePendingSocketLater(PendingUpgrade& pending);
    void removePending(const std::shared_ptr<PendingUpgrade>& pending);
    void queueConnection(std::unique_ptr<WebSocket> socket);

private:
    std::unique_ptr<TcpServer> m_tcpServer;
    std::vector<std::shared_ptr<PendingUpgrade>> m_pendingUpgrades;
    std::deque<std::unique_ptr<WebSocket>> m_pendingConnections;
};

}  // namespace snf
