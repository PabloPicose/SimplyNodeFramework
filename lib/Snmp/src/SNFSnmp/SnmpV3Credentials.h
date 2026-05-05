#pragma once

/**
 * @file SnmpV3Credentials.h
 * @brief SNMPv3 USM security credentials.
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpTypes.h"

#include <string>

namespace snf {

/**
 * @struct SnmpV3Credentials
 * @ingroup SNFSnmp
 * @brief Holds the User-based Security Model (USM) credentials for SNMPv3.
 *
 * Set `securityLevel` to control which fields are required:
 * | `securityLevel`  | `authProtocol` | `authPassphrase` | `privProtocol` | `privPassphrase` |
 * |------------------|----------------|------------------|----------------|------------------|
 * | `NoAuthNoPriv`   | `None`         | (ignored)        | `None`         | (ignored)        |
 * | `AuthNoPriv`     | required       | required         | `None`         | (ignored)        |
 * | `AuthPriv`       | required       | required         | required       | required         |
 *
 * @code
 * snf::SnmpV3Credentials creds;
 * creds.username       = "snmpAdmin";
 * creds.securityLevel  = snf::SnmpSecurityLevel::AuthPriv;
 * creds.authProtocol   = snf::SnmpAuthProtocol::SHA256;
 * creds.authPassphrase = "authSecret123!";
 * creds.privProtocol   = snf::SnmpPrivProtocol::AES128;
 * creds.privPassphrase = "privSecret123!";
 * @endcode
 */
struct SnmpV3Credentials
{
    /** @brief USM security name (username). */
    std::string username;

    /** @brief Desired security level. Defaults to @c NoAuthNoPriv. */
    SnmpSecurityLevel securityLevel = SnmpSecurityLevel::NoAuthNoPriv;

    /** @brief Authentication digest algorithm. Defaults to @c None. */
    SnmpAuthProtocol authProtocol = SnmpAuthProtocol::None;

    /**
     * @brief Authentication passphrase (minimum 8 characters per RFC 3414).
     * Required when `securityLevel` is `AuthNoPriv` or `AuthPriv`.
     */
    std::string authPassphrase;

    /** @brief Privacy (encryption) algorithm. Defaults to @c None. */
    SnmpPrivProtocol privProtocol = SnmpPrivProtocol::None;

    /**
     * @brief Privacy passphrase (minimum 8 characters per RFC 3414).
     * Required when `securityLevel` is `AuthPriv`.
     */
    std::string privPassphrase;
};

} // namespace snf
