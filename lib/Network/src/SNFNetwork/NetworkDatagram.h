#pragma once

/**
 * @file NetworkDatagram.h
 * @brief Container for datagram payload and sender address information.
 * @ingroup SNFNetwork_UDP
 */

#include "SNFNetwork/ByteArray.h"

#include <cstdint>
#include <string>

namespace snf {

/**
 * @class NetworkDatagram
 * @ingroup SNFNetwork_UDP
 * @brief Encapsulates a UDP datagram with sender address and port information.
 *
 * A `NetworkDatagram` is returned by `UdpSocket::pendingDatagram()` to provide
 * both the received payload and information about where it came from.
 */
class NetworkDatagram final
{
public:
    /**
     * @brief Constructs an empty datagram.
     */
    NetworkDatagram() = default;

    /**
     * @brief Constructs a datagram with data, sender host, and sender port.
     * @param data       The received payload.
     * @param senderHost The IP address of the sender.
     * @param senderPort The port of the sender.
     */
    NetworkDatagram(ByteArray data, std::string senderHost, std::uint16_t senderPort);

    /**
     * @brief Returns the datagram payload.
     */
    const ByteArray& data() const;

    /**
     * @brief Returns the IP address of the sender.
     */
    const std::string& senderHost() const;

    /**
     * @brief Returns the port of the sender.
     */
    std::uint16_t senderPort() const;

private:
    ByteArray m_data;
    std::string m_senderHost;
    std::uint16_t m_senderPort = 0;
};

}  // namespace snf
