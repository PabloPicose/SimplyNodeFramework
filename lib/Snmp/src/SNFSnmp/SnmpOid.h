#pragma once

/**
 * @file SnmpOid.h
 * @brief SNMP Object Identifier (OID) representation.
 * @ingroup SNFSnmp
 */

#include <cstdint>
#include <string>
#include <vector>

namespace snf {

/**
 * @class SnmpOid
 * @ingroup SNFSnmp
 * @brief Represents an SNMP Object Identifier as a sequence of unsigned integers.
 *
 * An OID uniquely identifies a managed object in the MIB tree, e.g.
 * `"1.3.6.1.2.1.1.1.0"` (sysDescr.0).
 *
 * @code
 * // Construct from dotted-decimal string
 * snf::SnmpOid sysDescr("1.3.6.1.2.1.1.1.0");
 *
 * // Construct from numeric components
 * snf::SnmpOid ifInOctets({1,3,6,1,2,1,2,2,1,10,1});
 *
 * std::cout << sysDescr.toString(); // "1.3.6.1.2.1.1.1.0"
 * @endcode
 */
class SnmpOid
{
public:
    SnmpOid() = default;

    /**
     * @brief Parses an OID from a dotted-decimal string, e.g. @c "1.3.6.1.2.1.1.1.0".
     *
     * Leading or trailing dots are ignored. An empty or invalid string produces
     * an invalid (empty) OID.
     */
    explicit SnmpOid(const std::string& dotted);

    /** @brief Constructs an OID from an explicit sequence of sub-identifiers. */
    explicit SnmpOid(std::vector<std::uint32_t> components);

    /** @brief Returns the dotted-decimal string representation, e.g. @c "1.3.6.1.2.1.1.1.0". */
    std::string toString() const;

    /** @brief Returns the raw numeric sub-identifiers. */
    const std::vector<std::uint32_t>& components() const noexcept;

    /** @brief Returns @c true if the OID has at least one sub-identifier. */
    bool isValid() const noexcept;

    bool operator==(const SnmpOid& other) const noexcept;
    bool operator!=(const SnmpOid& other) const noexcept;

    /**
     * @brief Lexicographic less-than, useful for sorting and use in @c std::map.
     */
    bool operator<(const SnmpOid& other) const noexcept;

    /**
     * @brief Returns @c true if this OID is a prefix of (or equal to) @p other.
     *
     * Useful when walking a subtree: stop when the next OID no longer starts
     * with the root OID.
     *
     * @code
     * SnmpOid root("1.3.6.1.2.1.2");
     * SnmpOid child("1.3.6.1.2.1.2.2.1.10.1");
     * assert(root.isPrefixOf(child) == true);
     * @endcode
     */
    bool isPrefixOf(const SnmpOid& other) const noexcept;

private:
    std::vector<std::uint32_t> m_components;
};

} // namespace snf
