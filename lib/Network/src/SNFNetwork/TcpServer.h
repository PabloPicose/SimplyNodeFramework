#pragma once

/**
 * @file TcpServer.h
 * @brief Non-blocking TCP server that accepts inbound connections.
 * @ingroup SNFNetwork_TCP
 */

#include "SNFNetwork/HostAddress.h"
#include "SNFNetwork/IOEvent.h"
#include "SNFNetwork/TcpSocket.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace snf {

/**
 * @class TcpServer
 * @ingroup SNFNetwork_TCP
 * @brief Listens for incoming TCP connections and queues them for acceptance.
 *
 * After calling `listen()`, the server watches its listening socket via the
 * owner thread's `EventLoop`. When a client connects, `newConnection` is
 * emitted. Retrieve the accepted socket with `nextPendingConnection()`.
 *
 * The accepted `TcpSocket*` is unparented (root node). The caller is
 * responsible for its lifetime: either adopt it into a node tree or
 * `delete`/`deleteLater()` it when done.
 *
 * @code
 * snf::TcpServer server;
 * server.listen(snf::HostAddress::AnyIPv4, 8080);
 *
 * server.newConnection.connect([&]() {
 *     snf::TcpSocket* peer = server.nextPendingConnection();
 *     peer->readyRead.connect([peer]() { peer->write(peer->readAll()); });
 *     peer->disconnected.connect([peer]() { peer->deleteLater(); });
 * });
 * @endcode
 */
class TcpServer final : public IOEvent
{
public:
    /** @brief Constructs a TcpServer, optionally attaching it to @p parent. */
    explicit TcpServer(Node* parent = nullptr);
    ~TcpServer() override;

    /**
     * @brief Starts listening for connections on the given address and port.
     * @param address Local address to bind (e.g. `HostAddress::AnyIPv4`).
     * @param port    Port number; pass 0 to let the OS pick an ephemeral port.
     * @return `true` on success, `false` on failure (check `errorOccurred`).
     */
    bool listen(const HostAddress& address, std::uint16_t port);

    /** @brief Overload accepting a raw host string. */
    bool listen(const std::string& address, std::uint16_t port);

    /**
     * @brief Stops listening and releases the socket.
     *
     * Pending connections that have already been accepted internally but not
     * yet retrieved via `nextPendingConnection()` are discarded.
     */
    void close();

    /** @brief Returns `true` if the server is currently listening. */
    bool isListening() const;

    /** @brief Returns `true` if at least one accepted connection is queued. */
    bool hasPendingConnections() const;

    /**
     * @brief Returns the next accepted `TcpSocket`, or `nullptr` if the
     *        queue is empty.
     *
     * Ownership transfers to the caller. The socket is a root node;
     * call `deleteLater()` or `delete` when finished with it.
     */
    TcpSocket* nextPendingConnection();

    /**
     * @brief Sets the maximum number of connections to queue internally
     *        before dropping new ones.
     *
     * Defaults to 30.
     */
    void setMaxPendingConnections(std::size_t maxPendingConnections);

    /** @brief Returns the configured pending-connection queue limit. */
    std::size_t maxPendingConnections() const;

    /** @brief Returns the port the server is bound to, or 0 if not listening. */
    std::uint16_t serverPort() const;

    /** @brief Returns the local address the server is bound to. */
    std::string serverAddress() const;

    Signal<>            newConnection;  ///< Emitted when a new connection is ready to accept.
    Signal<std::string> errorOccurred;  ///< Emitted with a message when a socket error occurs.

protected:
    /** @cond INTERNAL */
    void handleEvents(std::uint32_t nativeEvents) override;
    /** @endcond */

private:
    void acceptPendingConnections();
    void emitNewConnection();
    void emitErrorOccurred(std::string message);
    void failWithErrno(const std::string& prefix, int errorCode);

private:
    mutable std::mutex m_mutex;
    std::deque<TcpSocket*> m_pendingConnections;
    std::size_t m_maxPendingConnections = 30;
    bool m_listening = false;
    std::uint16_t m_serverPort = 0;
    std::string m_serverAddress;
};

}  // namespace snf
