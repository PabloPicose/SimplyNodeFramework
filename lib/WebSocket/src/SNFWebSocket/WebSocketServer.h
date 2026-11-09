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
 *
 * Connections accepted elsewhere (e.g. by `HttpServer` on its own port) can be
 * adopted with `handleUpgrade()`; this works with or without `listen()`.
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

    /**
     * @brief Adopts a connection whose Upgrade request was read by someone else.
     * @param socket Connected socket; ownership passes to this server.
     * @param alreadyRead Bytes already read from @p socket: the Upgrade request
     *        (possibly incomplete) followed by any bytes sent after it.
     *
     * The handshake is validated exactly as for connections accepted by
     * `listen()` (key, version 13, allowed origins). On success the 101
     * response is sent and the connection is queued (`newConnection`,
     * `nextPendingConnection()`). On failure an HTTP error is sent,
     * `errorOccurred` is emitted and the socket is closed and deleted.
     *
     * Must be called on this server's EventLoop thread.
     */
    void handleUpgrade(TcpSocket* socket, std::string alreadyRead);

    bool hasPendingConnections() const;
    WebSocket* nextPendingConnection();

    /**
     * @brief Restricts which browser origins may open a connection.
     *
     * Browsers do not apply CORS to WebSockets: any page the user visits can
     * open `ws://127.0.0.1:<port>`. Browsers always send an `Origin` header,
     * so when this list is non-empty an Upgrade request whose `Origin` is not
     * in it (compared case-insensitively, e.g. `"http://localhost:8080"`) is
     * answered with HTTP 403 and `errorOccurred` is emitted. Requests without
     * `Origin` (native clients such as `snf::WebSocket`) are still accepted.
     *
     * An empty list (the default) accepts every origin.
     */
    void setAllowedOrigins(std::vector<std::string> origins);
    const std::vector<std::string>& allowedOrigins() const;

    Signal<> newConnection;
    Signal<std::string> errorOccurred;

private:
    struct PendingUpgrade;

    void handleTcpNewConnection();
    std::shared_ptr<PendingUpgrade> adoptPending(std::unique_ptr<TcpSocket> socket);
    void processPendingBuffer(const std::shared_ptr<PendingUpgrade>& pending);
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
    std::vector<std::string> m_allowedOrigins;  // lower-cased
};

}  // namespace snf
