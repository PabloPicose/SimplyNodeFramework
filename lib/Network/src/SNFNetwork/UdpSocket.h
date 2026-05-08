#pragma once

/**
 * @file UdpSocket.h
 * @brief Non-blocking UDP socket with multicast group support.
 * @ingroup SNFNetwork_UDP
 */

#include "SNFNetwork/ByteArray.h"
#include "SNFNetwork/HostAddress.h"
#include "SNFNetwork/IOEvent.h"
#include "SNFNetwork/NetworkDatagram.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace snf {

/**
 * @enum UdpSocketState
 * @ingroup SNFNetwork_UDP
 * @brief Lifecycle state of a `UdpSocket`.
 */
enum class UdpSocketState {
    Unbound, ///< Socket not yet bound to a port.
    Bound,   ///< Socket bound to a port and ready for I/O.
    Error,   ///< An unrecoverable error occurred.
};

/**
 * @class UdpSocket
 * @ingroup SNFNetwork_UDP
 * @brief Non-blocking UDP socket with multicast support.
 *
 * `UdpSocket` is a datagram-oriented socket used for sending and receiving
 * UDP datagrams. Unlike `TcpSocket`, UDP is connectionless — there is no
 * connection phase. Simply call `bind()` to associate the socket with a local
 * port, then send and receive datagrams.
 *
 * Multicast support is available via `joinMulticastGroup()` and
 * `leaveMulticastGroup()`. When joined to a multicast group, the socket can
 * receive multicast datagrams addressed to the group.
 *
 * All operations are non-blocking by default (`blocking = false`). Data
 * readiness is signalled via `readyRead`. Outgoing datagrams are sent
 * asynchronously, with completion notified via `bytesWritten`.
 *
 * @note A `UdpSocket` is bound to its owner thread. All signal emissions and
 *       I/O callbacks occur on that thread.
 */
class UdpSocket final : public IOEvent
{
public:
    /**
     * @brief Constructs a new UDP socket.
     * @param blocking If `true`, operations block until complete. Default: `false`.
     * @param parent   Optional parent node.
     */
    explicit UdpSocket(bool blocking = false, Node* parent = nullptr);
    ~UdpSocket() override;

    /** @brief Switches the socket between blocking and non-blocking mode. */
    void setBlocking(bool blocking);

    /** @brief Returns `true` if the socket is in blocking mode. */
    bool isBlocking() const;

    /**
     * @brief Binds the socket to @p hostAddress on @p port.
     *
     * After binding successfully, the socket is in the `Bound` state and ready
     * to send and receive datagrams. If binding fails, `errorOccurred` is emitted
     * and the socket transitions to the `Error` state.
     *
     * @param hostAddress The local address to bind to. Use `HostAddress::AnyIPv4`
     *                    to bind to all IPv4 interfaces on a specific port.
     * @param port        The local port number. If `port` is 0, the OS assigns
     *                    an ephemeral port; call `boundPort()` to retrieve it.
     * @return `true` if binding succeeded, `false` if it failed.
     */
    bool bind(const HostAddress& hostAddress, std::uint16_t port);

    /**
     * @brief Returns the port to which the socket is bound.
     *
     * If the socket was bound with port 0, this returns the OS-assigned
     * ephemeral port. Returns 0 if the socket is not bound.
     */
    std::uint16_t boundPort() const;

    /**
     * @brief Sends a datagram to @p destAddress on @p destPort.
     * @param data        The payload to send.
     * @param destAddress The destination host address.
     * @param destPort    The destination port number.
     * @return Number of bytes queued for transmission.
     */
    std::size_t sendDatagram(const std::vector<std::uint8_t>& data,
                             const HostAddress& destAddress,
                             std::uint16_t destPort);

    /** @brief Overload accepting a raw byte span. */
    std::size_t sendDatagram(Span<const std::byte> data,
                             const HostAddress& destAddress,
                             std::uint16_t destPort);

    /** @brief Overload accepting a UTF-8 string payload. */
    std::size_t sendDatagram(const std::string& data, const HostAddress& destAddress, std::uint16_t destPort);

    /**
     * @brief Returns `true` if a datagram is available to read.
     *
     * Typically called from a `readyRead` slot.
     */
    bool hasPendingDatagram() const;

    /**
     * @brief Retrieves and removes the next available datagram.
     *
     * Returns an empty datagram if no data is available.
     */
    NetworkDatagram pendingDatagram();

    /**
     * @brief Joins a multicast group.
     *
     * @param groupAddress    The multicast group address (e.g., "239.0.0.1").
     * @param interfaceAddress The local interface to use. Use `HostAddress::AnyIPv4`
     *                         to let the OS choose.
     * @return `true` if the join succeeded, `false` otherwise.
     */
    bool joinMulticastGroup(const HostAddress& groupAddress, const HostAddress& interfaceAddress);

    /**
     * @brief Leaves a previously joined multicast group.
     *
     * @param groupAddress    The multicast group address.
     * @param interfaceAddress The local interface used when joining.
     * @return `true` if the leave succeeded, `false` otherwise.
     */
    bool leaveMulticastGroup(const HostAddress& groupAddress, const HostAddress& interfaceAddress);

    /** @brief Returns the current state of the socket. */
    UdpSocketState state() const;

    Signal<>            readyRead;     ///< Emitted when a datagram is available.
    Signal<std::size_t> bytesWritten;  ///< Emitted with bytes successfully sent.
    Signal<std::string> errorOccurred; ///< Emitted with a message when an error occurs.

protected:
    /** @cond INTERNAL */
    void handleEvents(std::uint32_t nativeEvents) override;
    /** @endcond */

private:
    bool sendDatagramToAddress(Span<const std::byte> data,
                               const sockaddr_storage& destAddress);
    void handleReadable();
    void handleWritable();
    void applyBoundState();
    void failWithErrno(const std::string& prefix, int errorCode);
    void updateInterestForState();
    void emitReadyRead();
    void emitBytesWritten(std::size_t written);
    void emitErrorOccurred(std::string message);
    void transitionToError();
    void transitionToUnbound();

private:
    mutable std::mutex m_mutex;
    bool m_blocking = false;
    UdpSocketState m_state = UdpSocketState::Unbound;
    std::uint16_t m_boundPort = 0;
    std::queue<NetworkDatagram> m_pendingDatagrams;
};

}  // namespace snf
