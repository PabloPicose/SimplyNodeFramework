#pragma once

/**
 * @file SnmpTrapInfo.h
 * @brief Payload delivered by SnmpTrapReceiver::trapReceived.
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpOid.h"
#include "SNFSnmp/SnmpTypes.h"
#include "SNFSnmp/SnmpVarBind.h"

#include <string>
#include <vector>

namespace snf {

/**
 * @struct SnmpTrapInfo
 * @ingroup SNFSnmp
 * @brief All information carried by an inbound SNMPv3 TRAP or INFORM PDU.
 *
 * - `trapOid`       — value of snmpTrapOID.0 from the varbind list.
 * - `sourceAddress` — dotted-decimal IP of the sending agent.
 * - `varbinds`      — all variable bindings from the PDU (excluding
 *                     sysUpTime.0 and snmpTrapOID.0, which are parsed
 *                     into dedicated fields).
 * - `securityName`  — USM username supplied by the sender.
 * - `securityLevel` — negotiated security level (auth/priv).
 * - `contextName`   — SNMPv3 context name (empty string if not set).
 */
struct SnmpTrapInfo
{
    SnmpOid                  trapOid;
    std::string              sourceAddress;
    std::vector<SnmpVarBind> varbinds;
    std::string              securityName;
    SnmpSecurityLevel        securityLevel = SnmpSecurityLevel::NoAuthNoPriv;
    std::string              contextName;
};

} // namespace snf
