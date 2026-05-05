#pragma once

/**
 * @file SnmpV3Config.h
 * @brief Target agent address and transport settings for SNMPv3.
 * @ingroup SNFSnmp
 */

#include <chrono>
#include <cstdint>
#include <string>

namespace snf {

/**
 * @struct SnmpV3Config
 * @ingroup SNFSnmp
 * @brief Configures the transport parameters when opening an SNMPv3 session.
 *
 * @code
 * snf::SnmpV3Config cfg;
 * cfg.host    = "192.168.10.1";
 * cfg.port    = 161;
 * cfg.timeout = std::chrono::seconds(5);
 * cfg.retries = 3;
 * @endcode
 */
struct SnmpV3Config
{
    /** @brief IP address or hostname of the SNMP agent. */
    std::string host;

    /** @brief UDP port the agent listens on. Defaults to 161. */
    std::uint16_t port = 161;

    /**
     * @brief Per-retry timeout.
     * net-snmp accepts this value in microseconds internally; the conversion
     * is handled automatically. Defaults to 5 seconds.
     */
    std::chrono::microseconds timeout = std::chrono::seconds(5);

    /**
     * @brief Maximum number of retransmission attempts before giving up.
     * Defaults to 3.
     */
    int retries = 3;

    /**
     * @brief Optional SNMPv3 context name.
     * Leave empty for the default context.
     */
    std::string contextName;

    /**
     * @brief Optional SNMPv3 context engine ID (hex-encoded string).
     * Leave empty to have the agent's engine ID discovered automatically.
     */
    std::string contextEngineId;
};

} // namespace snf
