#pragma once

/**
 * @file SnmpValue.h
 * @brief Typed value container for SNMP variable bindings.
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpOid.h"
#include "SNFSnmp/SnmpTypes.h"

#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace snf {

/**
 * @class SnmpValue
 * @ingroup SNFSnmp
 * @brief Holds a typed SNMP value as returned in GET/WALK responses or supplied
 *        to SET requests.
 *
 * The runtime type can be queried with `type()`. Convenience factory methods
 * construct typed values, and typed accessors retrieve the stored data.
 *
 * @code
 * // Build values for a SET request
 * snf::SnmpValue freq  = snf::SnmpValue::fromInteger32(134800);
 * snf::SnmpValue label = snf::SnmpValue::fromOctetString("RADIO-1");
 *
 * // Read a value from a GET response
 * if (vb.value.type() == snf::SnmpValueType::Integer32)
 *     int freq = vb.value.toInt32();
 * @endcode
 */
class SnmpValue
{
public:
    /** @brief Constructs a Null value. */
    SnmpValue() = default;

    // ── Factory methods ────────────────────────────────────────────────────

    static SnmpValue fromInteger32(std::int32_t v);
    static SnmpValue fromCounter32(std::uint32_t v);
    static SnmpValue fromGauge32(std::uint32_t v);
    static SnmpValue fromTimeTicks(std::uint32_t v);
    static SnmpValue fromCounter64(std::uint64_t v);

    /** @brief Constructs an OctetString value from raw bytes. */
    static SnmpValue fromOctetString(std::vector<std::uint8_t> bytes);

    /** @brief Constructs an OctetString value from a UTF-8 string. */
    static SnmpValue fromOctetString(const std::string& text);

    /** @brief Constructs an IpAddress value from a dotted-decimal string, e.g. @c "192.168.1.1". */
    static SnmpValue fromIpAddress(const std::string& dotted);

    static SnmpValue fromObjectIdentifier(SnmpOid oid);
    static SnmpValue fromOpaque(std::vector<std::uint8_t> bytes);

    /** @brief Constructs a Float value (net-snmp Opaque Float extension). */
    static SnmpValue fromFloat(float v);

    /** @brief Constructs a Double value (net-snmp Opaque Double extension). */
    static SnmpValue fromDouble(double v);

    /**
     * @brief Constructs a TruthValue Boolean (RFC 2579): @c true → INTEGER 1,
     *        @c false → INTEGER 2.  Wire type is @c Integer32.
     */
    static SnmpValue fromBool(bool v);

    // ── Type ──────────────────────────────────────────────────────────────

    /** @brief Returns the runtime type of this value. */
    SnmpValueType type() const noexcept;

    // ── Typed accessors ───────────────────────────────────────────────────

    /**
     * @brief Returns the Integer32 value.
     * @throws std::bad_variant_access if the type is not @c Integer32.
     */
    std::int32_t toInt32() const;

    /**
     * @brief Returns the unsigned 32-bit value (Counter32, Gauge32, or TimeTicks).
     * @throws std::bad_variant_access if the type is not one of those three.
     */
    std::uint32_t toUint32() const;

    /**
     * @brief Returns the Counter64 value.
     * @throws std::bad_variant_access if the type is not @c Counter64.
     */
    std::uint64_t toCounter64() const;

    /**
     * @brief Returns the OctetString or IpAddress as a @c std::string.
     *
     * - For @c OctetString, the bytes are reinterpreted as a UTF-8 string.
     * - For @c IpAddress, the stored dotted-decimal string is returned.
     * @throws std::bad_variant_access for other types.
     */
    std::string toString() const;

    /**
     * @brief Returns the raw byte buffer for OctetString or Opaque values.
     * @throws std::bad_variant_access for other types.
     */
    const std::vector<std::uint8_t>& toBytes() const;

    /**
     * @brief Returns the OID for an ObjectIdentifier value.
     * @throws std::bad_variant_access if the type is not @c ObjectIdentifier.
     */
    const SnmpOid& toOid() const;

    /**
     * @brief Returns the Float value.
     * @throws std::bad_variant_access if the type is not @c Float.
     */
    float toFloat() const;

    /**
     * @brief Returns the Double value.
     * @throws std::bad_variant_access if the type is not @c Double.
     */
    double toDouble() const;

    /**
     * @brief Interprets an @c Integer32 as a TruthValue (RFC 2579).
     * Returns @c true when the stored integer is 1.
     * @throws std::bad_variant_access if the type is not @c Integer32.
     */
    bool toBool() const;

    /** @brief A human-readable representation for logging and debugging. */
    std::string toDisplayString() const;

    // ── net-snmp interop (used by SnmpV3Session internals) ────────────────

    /**
     * @brief Returns the single-character type code expected by net-snmp's
     *        @c snmp_add_var() for SET operations.
     */
    char netSnmpTypeChar() const noexcept;

    /**
     * @brief Serialises the value to the string format expected by net-snmp's
     *        @c snmp_add_var() for SET operations.
     */
    std::string toNetSnmpString() const;

private:
    // Discriminated union: monostate = Null; uint32_t is shared by Counter32,
    // Gauge32, and TimeTicks (m_type disambiguates); bytes is shared by
    // OctetString, IpAddress (stored as dotted string bytes), and Opaque.
    using Storage = std::variant<
        std::monostate,            // Null
        std::int32_t,              // Integer32
        std::uint32_t,             // Counter32, Gauge32, TimeTicks
        std::uint64_t,             // Counter64
        std::vector<std::uint8_t>, // OctetString, Opaque
        std::string,               // IpAddress (dotted decimal)
        SnmpOid,                   // ObjectIdentifier
        float,                     // Float  (Opaque Float extension)
        double                     // Double (Opaque Double extension)
    >;

    SnmpValueType m_type    = SnmpValueType::Null;
    Storage       m_storage;
};

} // namespace snf
