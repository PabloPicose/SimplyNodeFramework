#pragma once

/**
 * @file HostAddress.h
 * @brief Host address representation and resolution helper.
 * @ingroup SNFNetwork_Util
 */

#include <cstdint>
#include <string>
#include <vector>

struct sockaddr_storage;

namespace snf {

/**
 * @enum HostResolveMode
 * @ingroup SNFNetwork_Util
 * @brief Selects whether the address should be resolved for connecting or binding.
 */
enum class HostResolveMode {
    Connect, ///< Resolve for outgoing connections.
    Bind,    ///< Resolve for server-side binding.
};

/**
 * @class HostAddress
 * @ingroup SNFNetwork_Util
 * @brief Wraps a hostname or IP address string and resolves it to
 *        `sockaddr_storage` entries via `getaddrinfo`.
 *
 * Several common addresses are provided as static constants:
 * @code
 * HostAddress::LocalHost     // "127.0.0.1"
 * HostAddress::LocalHostIPv6 // "::1"
 * HostAddress::AnyIPv4       // "0.0.0.0"  (bind to all IPv4 interfaces)
 * HostAddress::AnyIPv6       // "::"       (bind to all IPv6 interfaces)
 * @endcode
 */
class HostAddress final
{
public:
    /** @brief Constructs a HostAddress from a hostname or IP string. */
    explicit HostAddress(std::string host = {});

    static const HostAddress LocalHost;     ///< IPv4 loopback: `127.0.0.1`.
    static const HostAddress LocalHostIPv6; ///< IPv6 loopback: `::1`.
    static const HostAddress AnyIPv4;       ///< Bind to all IPv4 interfaces: `0.0.0.0`.
    static const HostAddress AnyIPv6;       ///< Bind to all IPv6 interfaces: `::`.  

    /** @brief Returns the raw hostname/IP string. */
    const std::string& host() const;

    /** @brief Returns `true` if no host string has been set. */
    bool isEmpty() const;

    /**
     * @brief Resolves the host string to a list of `sockaddr_storage` entries.
     *
     * @param port         Port number to embed in the resolved addresses.
     * @param mode         `Connect` or `Bind`; affects `AI_PASSIVE` hint.
     * @param addresses    Populated with all resolved addresses on success.
     * @param errorMessage Set to a human-readable error message on failure.
     * @return `true` on success, `false` if resolution failed.
     */
    bool resolve(std::uint16_t port,
                 HostResolveMode mode,
                 std::vector<sockaddr_storage>& addresses,
                 std::string& errorMessage) const;

private:
    std::string m_host;
};

}  // namespace snf