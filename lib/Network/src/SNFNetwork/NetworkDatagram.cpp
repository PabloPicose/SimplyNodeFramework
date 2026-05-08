#include "SNFNetwork/NetworkDatagram.h"

namespace snf {

NetworkDatagram::NetworkDatagram(ByteArray data,
                                 std::string senderHost,
                                 std::uint16_t senderPort)
    : m_data(std::move(data)), m_senderHost(std::move(senderHost)), m_senderPort(senderPort)
{
}

const ByteArray& NetworkDatagram::data() const
{
    return m_data;
}

const std::string& NetworkDatagram::senderHost() const
{
    return m_senderHost;
}

std::uint16_t NetworkDatagram::senderPort() const
{
    return m_senderPort;
}

}  // namespace snf
