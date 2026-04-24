#pragma once

/**
 * @file LocalServer.h
 * @brief Non-blocking Unix domain socket server.
 * @ingroup SNFNetwork_Unix
 */

#include "SNFNetwork/IOEvent.h"
#include "SNFNetwork/LocalSocket.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace snf {

/**
 * @class LocalServer
 * @ingroup SNFNetwork_Unix
 * @brief Listens on a Unix domain socket path and queues inbound connections.
 *
 * `LocalServer` mirrors the API of `TcpServer` for Unix domain sockets.
 * The socket file is created at the path passed to `listen()` and is
 * automatically removed when `close()` is called or the server is destroyed.
 *
 * @code
 * snf::LocalServer server;
 * server.listen("/tmp/my_app.sock");
 *
 * server.newConnection.connect([&]() {
 *     snf::LocalSocket* peer = server.nextPendingConnection();
 *     peer->readyRead.connect([peer]() { peer->write(peer->readAll()); });
 *     peer->disconnected.connect([peer]() { peer->deleteLater(); });
 * });
 * @endcode
 */
class LocalServer final : public IOEvent
{
public:
    /** @brief Constructs a LocalServer, optionally attaching it to @p parent. */
    explicit LocalServer(Node* parent = nullptr);
    ~LocalServer() override;

    /**
     * @brief Starts listening on the Unix domain socket at @p path.
     *
     * Creates the socket file. If a file already exists at @p path the call
     * will fail; remove it manually beforehand if required.
     *
     * @param path Filesystem path for the Unix domain socket.
     * @return `true` on success, `false` on failure.
     */
    bool listen(const std::string& path);

    /**
     * @brief Stops listening and removes the socket file.
     */
    void close();

    /** @brief Returns `true` if the server is currently listening. */
    bool isListening() const;

    /** @brief Returns `true` if at least one accepted connection is queued. */
    bool hasPendingConnections() const;

    /**
     * @brief Returns the next accepted `LocalSocket`, or `nullptr` if the
     *        queue is empty.
     *
     * Ownership transfers to the caller.
     */
    LocalSocket* nextPendingConnection();

    /**
     * @brief Sets the maximum number of connections to queue internally.
     *
     * Defaults to 30.
     */
    void setMaxPendingConnections(std::size_t maxPendingConnections);

    /** @brief Returns the configured pending-connection queue limit. */
    std::size_t maxPendingConnections() const;

    /** @brief Returns the filesystem path the server is bound to. */
    std::string serverPath() const;

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
    void unlinkPath();

private:
    mutable std::mutex m_mutex;
    std::deque<LocalSocket*> m_pendingConnections;
    std::size_t m_maxPendingConnections = 30;
    bool m_listening = false;
    std::string m_serverPath;
};

}  // namespace snf
