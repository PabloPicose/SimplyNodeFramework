#pragma once

/**
 * @file SnmpVarBind.h
 * @brief OID/value pair (variable binding) for SNMP requests and responses.
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpOid.h"
#include "SNFSnmp/SnmpValue.h"

namespace snf {

/**
 * @struct SnmpVarBind
 * @ingroup SNFSnmp
 * @brief Associates an OID with its typed value, as used in GET/SET/WALK PDUs.
 *
 * @code
 * // Building a SET request
 * snf::SnmpVarBind vb;
 * vb.oid   = snf::SnmpOid("1.3.6.1.4.1.12345.1.1.0"); // radioFrequency.0
 * vb.value = snf::SnmpValue::fromInteger32(134800);    // 134.800 MHz (kHz units)
 *
 * session->set(vb);
 *
 * // Reading a GET response
 * session->getResult.connect([](const std::vector<snf::SnmpVarBind>& vbs) {
 *     for (const auto& vb : vbs)
 *         std::cout << vb.oid.toString() << " = " << vb.value.toDisplayString() << '\n';
 * });
 * @endcode
 */
struct SnmpVarBind
{
    SnmpOid   oid;
    SnmpValue value;
};

} // namespace snf
