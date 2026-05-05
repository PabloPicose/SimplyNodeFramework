#pragma once

/**
 * @file SnmpTypes.h
 * @brief Core SNMP type enumerations used throughout SNFSnmp.
 * @ingroup SNFSnmp
 */

#include <cstdint>

namespace snf {

/**
 * @enum SnmpSecurityLevel
 * @ingroup SNFSnmp
 * @brief SNMPv3 USM security level, as defined in RFC 3414.
 */
enum class SnmpSecurityLevel {
    NoAuthNoPriv, ///< No authentication, no privacy.
    AuthNoPriv,   ///< Authentication only (no encryption).
    AuthPriv,     ///< Authentication and privacy (encryption).
};

/**
 * @enum SnmpAuthProtocol
 * @ingroup SNFSnmp
 * @brief Digest algorithm used by the SNMPv3 USM authentication subsystem.
 */
enum class SnmpAuthProtocol {
    None,   ///< No authentication (used with @c NoAuthNoPriv).
    MD5,    ///< HMAC-MD5-96  (RFC 3414).
    SHA1,   ///< HMAC-SHA-96  (RFC 3414).
    SHA256, ///< HMAC-SHA-2-192 (RFC 7860).
    SHA512, ///< HMAC-SHA-2-384 (RFC 7860).
};

/**
 * @enum SnmpPrivProtocol
 * @ingroup SNFSnmp
 * @brief Encryption algorithm used by the SNMPv3 USM privacy subsystem.
 */
enum class SnmpPrivProtocol {
    None,   ///< No privacy (used with @c NoAuthNoPriv or @c AuthNoPriv).
    DES,    ///< CBC-DES Symmetric Encryption Protocol (RFC 3414).
    AES128, ///< AES-128 CFB (RFC 3826).
    AES256, ///< AES-256 CFB (Blumenthal draft / Cisco extension).
};

/**
 * @enum SnmpValueType
 * @ingroup SNFSnmp
 * @brief Runtime type tag of an @c SnmpValue.
 */
enum class SnmpValueType {
    Null,             ///< SNMPv2 NULL (e.g., used in GET request varbinds).
    Integer32,        ///< 32-bit signed integer (SMIv2 INTEGER).
    Counter32,        ///< 32-bit unsigned monotonic counter (SMIv2 Counter32).
    Gauge32,          ///< 32-bit unsigned gauge (SMIv2 Gauge32 / Unsigned32).
    TimeTicks,        ///< Hundredths of a second since some epoch (SMIv2 TimeTicks).
    Counter64,        ///< 64-bit unsigned monotonic counter (SMIv2 Counter64).
    OctetString,      ///< Raw byte string (SMIv2 OCTET STRING).
    IpAddress,        ///< IPv4 address stored as a dotted-decimal string.
    ObjectIdentifier, ///< OID value (SMIv2 OBJECT IDENTIFIER).
    Opaque,           ///< Arbitrary opaque bytes.
    Float,            ///< 32-bit IEEE 754 float (net-snmp Opaque Float extension).
    Double,           ///< 64-bit IEEE 754 double (net-snmp Opaque Double extension).
};

} // namespace snf
