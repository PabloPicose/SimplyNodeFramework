#pragma once

/**
 * @file LocalSocket.h
 * @brief Non-blocking Unix domain socket client and accepted-connection socket.
 * @ingroup SNFNetwork_Unix
 */

#include "SNFNetwork/ByteArray.h"
#include "SNFNetwork/IOEvent.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace snf {

/**
 * @enum LocalSocketState
 * @ingroup SNFNetwork_Unix
 * @brief Lifecycle state of a `LocalSocket`.
 */
enum class LocalSocketState {
    Disconnected, ///< No active connection.
    Connecting,   ///< `connectToPath()` called; waiting for completion.
    Connected,    ///< Fully connected and ready for I/O.
    Error,        ///< An unrecoverable error occurred.
};

/**
 * @class LocalSocket
 * @ingroup SNFNetwork_Unix
 * @brief Non-blocking Unix domain socket for outgoing connections and accepted peers.
 *
 * `LocalSocket` mirrors `TcpSocket` for Unix domain sockets. Use
 * `connectToPath()` to connect to a `LocalServer`, or obtain an instance
 * from `LocalServer::nextPendingConnection()` for server-side peers.
 *
 * @note A `LocalSocket` is bound to its owner thread.
 */
class LocalSocket final : public IOEvent
{
public:
    /**
     * @brief Constructs a new client Unix domain socket.
     * @param blocking Whether operations block until complete. Default: `false`.
     * @param parent   Optional parent node.
     */
    explicit LocalSocket(bool blocking = false, Node* parent = nullptr);

    /**
     * @brief Wraps an already-connected file descriptor (used by `LocalServer`).
     * @param adoptedFd An open, connected socket file descriptor.
     * @param blocking  Whether the fd should be set to blocking mode.
     * @param parent    Optional parent node.
     */
    explicit LocalSocket(int adoptedFd, bool blocking = false, Node* parent = nullptr);
    ~LocalSocket() override;

    /** @brief Switches the socket between blocking and non-blocking mode. */
    void setBlocking(bool blocking);

    /** @brief Returns `true` if the socket is in blocking mode. */
    bool isBlocking() const;

    /**
     * @brief Initiates a non-blocking connection to the Unix domain socket
     *        at @p path.
     *
     * The `connected` signal is emitted when the connection is established.
     * If it fails, `errorOccurred` is emitted.
     */
    void connectToPath(const std::string& path);

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
    std::size_t write(ByteArray data);

    /** @brief Overload accepting an unsigned-byte vector. */
    std::size_t write(const std::vector<std::uint8_t>& data);

    /** @brief Overload accepting a UTF-8 string. */
    std::size_t write(const std::string& data);

    /**
     * @brief Reads and returns all data currently in the read buffer.
     *
     * Typically called from a `readyRead` slot.
     */
    ByteArray readAll();

    /** @brief Returns the current connection state. */
    LocalSocketState state() const;

    Signal<>            connected;     ///< Emitted when the connection is established.
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
    LocalSocketState m_state = LocalSocketState::Disconnected;
    ByteArray::Storage m_readBuffer;
    std::deque<ByteArray> m_writeQueue;
};

}  // namespace snf
