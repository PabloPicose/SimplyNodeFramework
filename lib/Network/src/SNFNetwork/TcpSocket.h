#pragma once

/**
 * @file TcpSocket.h
 * @brief Non-blocking TCP client socket and accepted-connection socket.
 * @ingroup SNFNetwork_TCP
 */

#include "SNFNetwork/HostAddress.h"
#include "SNFNetwork/IOEvent.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace snf {

/**
 * @enum TcpSocketState
 * @ingroup SNFNetwork_TCP
 * @brief Lifecycle state of a `TcpSocket`.
 */
enum class TcpSocketState {
    Disconnected, ///< No active connection.
    Connecting,   ///< `connect()` called; waiting for completion.
    Connected,    ///< Fully connected and ready for I/O.
    Error,        ///< An unrecoverable error occurred.
};

/**
 * @class TcpSocket
 * @ingroup SNFNetwork_TCP
 * @brief Non-blocking TCP socket for outgoing connections and accepted peers.
 *
 * `TcpSocket` can be used in two ways:
 * - **Client** — call `connectToHost()` to initiate an outgoing TCP connection.
 * - **Server-side peer** — obtained from `TcpServer::nextPendingConnection()`;
 *   the socket is already in the `Connected` state.
 *
 * All operations are non-blocking by default (`blocking = false`). Data
 * readiness is signalled via `readyRead`. Outgoing data is buffered and
 * flushed asynchronously; completion is notified via `bytesWritten`.
 *
 * @note A `TcpSocket` is bound to its owner thread. All signal emissions and
 *       I/O callbacks occur on that thread.
 */
class TcpSocket final : public IOEvent
{
public:
    /**
     * @brief Constructs a new client TCP socket.
     * @param blocking If `true`, operations block until complete. Default: `false`.
     * @param parent   Optional parent node.
     */
    explicit TcpSocket(bool blocking = false, Node* parent = nullptr);

    /**
     * @brief Wraps an already-connected file descriptor (used by `TcpServer`).
     * @param adoptedFd An open, connected socket file descriptor.
     * @param blocking  Whether the fd should be set to blocking mode.
     * @param parent    Optional parent node.
     */
    explicit TcpSocket(int adoptedFd, bool blocking = false, Node* parent = nullptr);
    ~TcpSocket() override;

    /** @brief Switches the socket between blocking and non-blocking mode. */
    void setBlocking(bool blocking);

    /** @brief Returns `true` if the socket is in blocking mode. */
    bool isBlocking() const;

    /**
     * @brief Initiates a non-blocking connection to @p hostAddress on @p port.
     *
     * The `connected` signal is emitted when the handshake completes.
     * If the connection fails, `errorOccurred` is emitted instead.
     */
    void connectToHost(const HostAddress& hostAddress, std::uint16_t port);

    /** @brief Overload accepting a raw host string. */
    void connectToHost(const std::string& host, std::uint16_t port);

    /**
     * @brief Closes the connection and resets the socket to `Disconnected`.
     *
     * `disconnected` is emitted if the socket was connected.
     */
    void close();

    /**
     * @brief Queues @p data for asynchronous transmission.
     * @return Number of bytes accepted into the write buffer.
     */
    std::size_t write(const std::vector<std::uint8_t>& data);

    /** @brief Overload accepting a UTF-8 string. */
    std::size_t write(const std::string& data);

    /**
     * @brief Reads and returns all data currently in the read buffer.
     *
     * Typically called from a `readyRead` slot. Returns an empty vector if
     * no data is available.
     */
    std::vector<std::uint8_t> readAll();

    /** @brief Returns the current connection state. */
    TcpSocketState state() const;

    Signal<>            connected;     ///< Emitted when the TCP handshake completes.
    Signal<>            disconnected;  ///< Emitted when the connection is closed.
    Signal<>            readyRead;     ///< Emitted when data is available in the read buffer.
    Signal<std::size_t> bytesWritten;  ///< Emitted with the number of bytes successfully sent.
    Signal<std::string> errorOccurred; ///< Emitted with a message when an error occurs.

protected:
    /** @cond INTERNAL */
    void handleEvents(std::uint32_t nativeEvents) override;
    /** @endcond */

private:
    bool flushPendingWrites();
    void handleReadable();
    void handleWritable();
    void applyConnectedState();
    void applyDisconnectedState(bool emitSignal);
    bool transitionToDisconnected(bool emitSignal);
    void failWithErrno(const std::string& prefix, int errorCode);
    void updateInterestForState();
    void emitConnected();
    void emitDisconnected();
    void emitReadyRead();
    void emitBytesWritten(std::size_t written);
    void emitErrorOccurred(std::string message);
    void adoptConnectedDescriptor(int fd);

private:
    mutable std::mutex m_mutex;
    bool m_blocking = false;
    TcpSocketState m_state = TcpSocketState::Disconnected;
    std::vector<std::uint8_t> m_readBuffer;
    std::vector<std::uint8_t> m_writeBuffer;
};

}  // namespace snf
