/**
 * @file SnmpTrapReceiver.cpp
 * @ingroup SNFSnmp
 *
 * Implementation notes
 * ────────────────────
 * The receiver uses net-snmp's single-session API (snmp_sess_add / snmp_sess_read)
 * which does not touch the global session list and therefore does not interfere
 * with concurrent SnmpV3Session GET/SET/WALK operations.
 *
 * Threading model
 * ───────────────
 * No background thread is created. After start() the underlying UDP socket fd
 * is registered with the owning EventLoop via registerIO(). When epoll signals
 * EPOLLIN the owner thread calls snmp_sess_read(), which parses and authenticates
 * the PDU and invokes pduCallback() synchronously — all on the owner thread.
 *
 * USM user registration
 * ──────────────────────
 * For SNMPv3 authentication net-snmp looks up USM users from its global user
 * list. We populate that list directly by calling usm_create_user() and
 * usm_add_user() with pre-derived keys (exactly as openSession() does for the
 * client side).  The engineID for incoming TRAPs is left empty (zero-length)
 * so that net-snmp accepts TRAPs from any engine — as required for a trap
 * receiver that does not know the sender's engineID in advance.
 */

// net-snmp headers MUST come first.
#include <net-snmp/net-snmp-config.h>   // NOLINT
#include <net-snmp/net-snmp-includes.h> // NOLINT

#ifdef NETSNMP_DRAFT_BLUMENTHAL_AES_04
#  ifndef USM_AUTH_PROTO_SHA256_LEN
#    define USM_AUTH_PROTO_SHA256_LEN OID_LENGTH(usmHMAC192SHA256AuthProtocol)
#  endif
#  ifndef USM_AUTH_PROTO_SHA512_LEN
#    define USM_AUTH_PROTO_SHA512_LEN OID_LENGTH(usmHMAC384SHA512AuthProtocol)
#  endif
#  ifndef USM_PRIV_PROTO_AES256_LEN
#    define USM_PRIV_PROTO_AES256_LEN OID_LENGTH(usmAES256PrivProtocol)
#  endif
#endif

#include "SNFSnmp/SnmpTrapReceiver.h"

#include <SNFCore/EventLoop.h>
#include <SNFCore/NodePtr.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace snf {

// ── Helpers shared with SnmpV3Session (duplicated to avoid exposing
//    the anonymous namespace / internal linkage symbols) ──────────────────

namespace {

std::once_flag g_snmpTrapInitFlag;

void snmpGlobalInit()
{
    std::call_once(g_snmpTrapInitFlag, [] {
        init_snmp("SNFSnmp");
    });
}

// These OIDs define the well-known SNMPv2-MIB varbinds prepended to every
// SNMPv2-TRAP and INFORM PDU (sysUpTime.0, snmpTrapOID.0).
// RFC 3418 / RFC 2578.
const oid k_sysUpTime[]  = { 1,3,6,1,2,1,1,3,0 };
const oid k_snmpTrapOID[] = { 1,3,6,1,6,3,1,1,4,1,0 };

constexpr std::size_t k_sysUpTimeLen  = OID_LENGTH(k_sysUpTime);
constexpr std::size_t k_snmpTrapOIDLen = OID_LENGTH(k_snmpTrapOID);

// Re-declare the authInfo/privInfo helpers from the session file so we can
// derive keys. To avoid duplication we reproduce the minimal subset here.

struct AuthEntry { const oid* proto; std::size_t len; };
struct PrivEntry { const oid* proto; std::size_t len; };

AuthEntry authFor(SnmpAuthProtocol p)
{
    switch (p) {
        case SnmpAuthProtocol::MD5:    return { usmHMACMD5AuthProtocol,  USM_AUTH_PROTO_MD5_LEN  };
        case SnmpAuthProtocol::SHA1:   return { usmHMACSHA1AuthProtocol, USM_AUTH_PROTO_SHA_LEN  };
#ifdef NETSNMP_DRAFT_BLUMENTHAL_AES_04
        case SnmpAuthProtocol::SHA256: return { usmHMAC192SHA256AuthProtocol, USM_AUTH_PROTO_SHA256_LEN };
        case SnmpAuthProtocol::SHA512: return { usmHMAC384SHA512AuthProtocol, USM_AUTH_PROTO_SHA512_LEN };
#else
        case SnmpAuthProtocol::SHA256: return { usmHMACSHA1AuthProtocol, USM_AUTH_PROTO_SHA_LEN  };
        case SnmpAuthProtocol::SHA512: return { usmHMACSHA1AuthProtocol, USM_AUTH_PROTO_SHA_LEN  };
#endif
        default:                       return { usmNoAuthProtocol, USM_AUTH_PROTO_NOAUTH_LEN };
    }
}

PrivEntry privFor(SnmpPrivProtocol p)
{
    switch (p) {
        case SnmpPrivProtocol::DES:    return { usmDESPrivProtocol,  USM_PRIV_PROTO_DES_LEN };
        case SnmpPrivProtocol::AES128: return { usmAESPrivProtocol,  USM_PRIV_PROTO_AES_LEN };
#ifdef NETSNMP_DRAFT_BLUMENTHAL_AES_04
        case SnmpPrivProtocol::AES256: return { usmAES256PrivProtocol, USM_PRIV_PROTO_AES256_LEN };
#else
        case SnmpPrivProtocol::AES256: return { usmAESPrivProtocol,  USM_PRIV_PROTO_AES_LEN };
#endif
        default:                       return { usmNoPrivProtocol, USM_PRIV_PROTO_NOPRIV_LEN };
    }
}

// Convert netsnmp_variable_list chain to SnmpVarBind vector.
// Identical logic to SnmpV3Session::parseVarBinds (kept local to avoid
// exposing the symbol from the other translation unit).
std::vector<SnmpVarBind> parseVarBinds(netsnmp_variable_list* vars)
{
    std::vector<SnmpVarBind> result;
    for (; vars; vars = vars->next_variable) {
        SnmpVarBind vb;
        std::vector<std::uint32_t> oidComps(vars->name, vars->name + vars->name_length);
        vb.oid = SnmpOid(std::move(oidComps));

        switch (vars->type) {
            case ASN_INTEGER:
                vb.value = SnmpValue::fromInteger32(static_cast<std::int32_t>(*vars->val.integer));
                break;
            case ASN_COUNTER:
                vb.value = SnmpValue::fromCounter32(static_cast<std::uint32_t>(*vars->val.integer));
                break;
            case ASN_GAUGE:
                vb.value = SnmpValue::fromGauge32(static_cast<std::uint32_t>(*vars->val.integer));
                break;
            case ASN_TIMETICKS:
                vb.value = SnmpValue::fromTimeTicks(static_cast<std::uint32_t>(*vars->val.integer));
                break;
            case ASN_COUNTER64: {
                const std::uint64_t v =
                    (static_cast<std::uint64_t>(vars->val.counter64->high) << 32) |
                     static_cast<std::uint64_t>(vars->val.counter64->low);
                vb.value = SnmpValue::fromCounter64(v);
                break;
            }
            case ASN_OCTET_STR: {
                std::vector<std::uint8_t> bytes(vars->val.string, vars->val.string + vars->val_len);
                vb.value = SnmpValue::fromOctetString(std::move(bytes));
                break;
            }
            case ASN_IPADDRESS: {
                char ipbuf[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, vars->val.string, ipbuf, sizeof(ipbuf));
                vb.value = SnmpValue::fromIpAddress(ipbuf);
                break;
            }
            case ASN_OBJECT_ID: {
                std::vector<std::uint32_t> oidVal(
                    vars->val.objid,
                    vars->val.objid + vars->val_len / sizeof(oid));
                vb.value = SnmpValue::fromObjectIdentifier(SnmpOid(std::move(oidVal)));
                break;
            }
            case ASN_OPAQUE: {
                std::vector<std::uint8_t> bytes(vars->val.string, vars->val.string + vars->val_len);
                vb.value = SnmpValue::fromOpaque(std::move(bytes));
                break;
            }
            case ASN_OPAQUE_FLOAT:
                vb.value = SnmpValue::fromFloat(*vars->val.floatVal);
                break;
            case ASN_OPAQUE_DOUBLE:
                vb.value = SnmpValue::fromDouble(*vars->val.doubleVal);
                break;
            default:
                break;
        }
        result.push_back(std::move(vb));
    }
    return result;
}

} // anonymous namespace

// Forward declaration of the file-static PDU callback (defined below).
static int trapPduCallback(int, netsnmp_session*, int, netsnmp_pdu*, void*);

// ── SnmpTrapReceiver ──────────────────────────────────────────────────────

SnmpTrapReceiver::SnmpTrapReceiver(Node* parent)
    : Node(parent)
{
    snmpGlobalInit();
}

SnmpTrapReceiver::~SnmpTrapReceiver()
{
    stop();
}

void SnmpTrapReceiver::setListenPort(uint16_t port)
{
    m_port = port;
}

void SnmpTrapReceiver::addV3User(const SnmpV3Credentials& creds)
{
    m_users.push_back({creds, {}});
}

void SnmpTrapReceiver::addV3User(const SnmpV3Credentials& creds,
                                  const std::vector<uint8_t>& senderEngineId)
{
    m_users.push_back({creds, senderEngineId});
}

bool SnmpTrapReceiver::start()
{
    if (m_listening)
        return true;

    // ── Register USM users in net-snmp's global list ──────────────────────
    for (const auto& entry : m_users) {
        const auto& u = entry.creds;

        struct usmUser* usr = usm_create_user();
        if (!usr) {
            const std::string err = "usm_create_user() failed for user: " + u.username;
            errorOccurred.emit(err);
            return false;
        }

        usr->name    = strdup(u.username.c_str());
        usr->secName = strdup(u.username.c_str());

        // Engine ID: set the specific sender engine ID when provided,
        // otherwise leave empty (matches any engine for noAuth traps).
        if (!entry.senderEngineId.empty()) {
            usr->engineIDLen = entry.senderEngineId.size();
            usr->engineID    = static_cast<u_char*>(malloc(usr->engineIDLen));
            memcpy(usr->engineID, entry.senderEngineId.data(), usr->engineIDLen);
        } else {
            usr->engineID    = nullptr;
            usr->engineIDLen = 0;
        }

        if (u.securityLevel != SnmpSecurityLevel::NoAuthNoPriv &&
            u.authProtocol  != SnmpAuthProtocol::None) {
            const auto ae = authFor(u.authProtocol);

            usr->authProtocol    = static_cast<oid*>(
                snmp_duplicate_objid(ae.proto, ae.len));
            usr->authProtocolLen = ae.len;

            // Derive the non-localized Ku from the passphrase.
            u_char   Ku[USM_AUTH_KU_LEN];
            size_t   KuLen = USM_AUTH_KU_LEN;
            if (generate_Ku(usr->authProtocol,
                            static_cast<u_int>(usr->authProtocolLen),
                            reinterpret_cast<u_char*>(
                                const_cast<char*>(u.authPassphrase.c_str())),
                            u.authPassphrase.size(),
                            Ku, &KuLen) != SNMPERR_SUCCESS) {
                usm_free_user(usr);
                errorOccurred.emit("Failed to derive auth key for user: " + u.username);
                return false;
            }

            usr->authKey    = static_cast<u_char*>(malloc(USM_AUTH_KU_LEN));
            usr->authKeyLen = USM_AUTH_KU_LEN;

            if (!entry.senderEngineId.empty()) {
                // Localize Ku → Kul using the sender's engine ID.
                // net-snmp verifies incoming HMACs against the localized key.
                if (generate_kul(usr->authProtocol, static_cast<u_int>(usr->authProtocolLen),
                                 usr->engineID, usr->engineIDLen,
                                 Ku, KuLen,
                                 usr->authKey, &usr->authKeyLen) != SNMPERR_SUCCESS) {
                    usm_free_user(usr);
                    errorOccurred.emit("Failed to localize auth key for user: " + u.username);
                    return false;
                }
            } else {
                memcpy(usr->authKey, Ku, KuLen);
                usr->authKeyLen = KuLen;
            }
        }

        if (u.securityLevel == SnmpSecurityLevel::AuthPriv &&
            u.privProtocol  != SnmpPrivProtocol::None) {
            const auto pe  = privFor(u.privProtocol);
            const auto ae  = authFor(u.authProtocol);

            usr->privProtocol    = static_cast<oid*>(
                snmp_duplicate_objid(pe.proto, pe.len));
            usr->privProtocolLen = pe.len;

            // Derive the non-localized priv Ku (using the AUTH hash, per RFC 3414).
            u_char   KuPriv[USM_PRIV_KU_LEN];
            size_t   KuPrivLen = USM_PRIV_KU_LEN;
            if (generate_Ku(const_cast<oid*>(ae.proto),
                            static_cast<u_int>(ae.len),
                            reinterpret_cast<u_char*>(
                                const_cast<char*>(u.privPassphrase.c_str())),
                            u.privPassphrase.size(),
                            KuPriv, &KuPrivLen) != SNMPERR_SUCCESS) {
                usm_free_user(usr);
                errorOccurred.emit("Failed to derive priv key for user: " + u.username);
                return false;
            }

            usr->privKey    = static_cast<u_char*>(malloc(USM_PRIV_KU_LEN));
            usr->privKeyLen = USM_PRIV_KU_LEN;

            if (!entry.senderEngineId.empty()) {
                // Localize the priv key using the same auth-protocol hash.
                if (generate_kul(const_cast<oid*>(ae.proto), static_cast<u_int>(ae.len),
                                 usr->engineID, usr->engineIDLen,
                                 KuPriv, KuPrivLen,
                                 usr->privKey, &usr->privKeyLen) != SNMPERR_SUCCESS) {
                    usm_free_user(usr);
                    errorOccurred.emit("Failed to localize priv key for user: " + u.username);
                    return false;
                }
            } else {
                memcpy(usr->privKey, KuPriv, KuPrivLen);
                usr->privKeyLen = KuPrivLen;
            }
        }

        usm_add_user(usr);
    }

    // ── Open the listening UDP transport ────────────────────────────────────
    const std::string transportSpec = "udp:0.0.0.0:" + std::to_string(m_port);
    netsnmp_transport* transport =
        netsnmp_tdomain_transport(transportSpec.c_str(), /*local=*/1, "udp");
    if (!transport) {
        errorOccurred.emit("Failed to create UDP transport on port " +
                           std::to_string(m_port) + ": check privileges");
        return false;
    }

    // ── Open a single net-snmp session in server / receive mode ─────────────
    netsnmp_session tmpl = {};
    snmp_sess_init(&tmpl);
    tmpl.version       = SNMP_DEFAULT_VERSION; // accept any version
    tmpl.callback       = &trapPduCallback;
    tmpl.callback_magic = this;

    m_sessp = snmp_sess_add(&tmpl, transport,
                            /*fpre_parse=*/nullptr,
                            /*fpost_parse=*/nullptr);
    if (!m_sessp) {
        char*  errstr = nullptr;
        int    liberr = 0, syserr = 0;
        snmp_error(&tmpl, &liberr, &syserr, &errstr);
        const std::string err = errstr ? std::string(errstr) : "snmp_sess_add() failed";
        SNMP_FREE(errstr);
        errorOccurred.emit(err);
        return false;
    }

    // Extract the underlying file descriptor.
    netsnmp_transport* t = snmp_sess_transport(m_sessp);
    if (!t || t->sock < 0) {
        errorOccurred.emit("Could not obtain socket fd from transport");
        snmp_sess_close(m_sessp);
        m_sessp = nullptr;
        return false;
    }
    m_sockFd = t->sock;

    // Put the socket in non-blocking mode so onSocketReady() never blocks.
    const int flags = fcntl(m_sockFd, F_GETFL, 0);
    if (flags != -1)
        fcntl(m_sockFd, F_SETFL, flags | O_NONBLOCK);

    // ── Register with the EventLoop ──────────────────────────────────────────
    registerWithLoop(ownerEventLoop());

    m_listening = true;
    return true;
}

void SnmpTrapReceiver::stop()
{
    if (!m_listening)
        return;

    unregisterFromLoop(ownerEventLoop());

    if (m_sessp) {
        snmp_sess_close(m_sessp);
        m_sessp  = nullptr;
    }
    m_sockFd    = -1;
    m_listening = false;
}

bool SnmpTrapReceiver::isListening() const noexcept
{
    return m_listening;
}

// ── EventLoop I/O integration ─────────────────────────────────────────────

void SnmpTrapReceiver::registerWithLoop(EventLoop* loop)
{
    if (!loop || m_sockFd < 0)
        return;

    loop->registerIO(m_sockFd, EPOLLIN,
        [self = NodePtr<SnmpTrapReceiver>(this)](std::uint32_t events) {
            if (self && !self.isMarkedToDelete())
                self->onSocketReady(events);
        });
}

void SnmpTrapReceiver::unregisterFromLoop(EventLoop* loop)
{
    if (loop && m_sockFd >= 0 && loop->hasIOWatch(m_sockFd))
        loop->unregisterIO(m_sockFd);
}

void SnmpTrapReceiver::onAboutToMoveToThread(EventLoop* /*newLoop*/)
{
    unregisterFromLoop(ownerEventLoop());
}

void SnmpTrapReceiver::onMovedToThread(EventLoop* /*oldLoop*/)
{
    registerWithLoop(ownerEventLoop());
}

// ── Socket readable callback ─────────────────────────────────────────────

void SnmpTrapReceiver::onSocketReady(std::uint32_t /*events*/)
{
    if (!m_sessp)
        return;

    // Build an fd_set containing just our socket and hand it to net-snmp.
    // snmp_sess_read() parses the packet, authenticates/decrypts it (USM),
    // and synchronously calls pduCallback() below before returning.
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(m_sockFd, &fdset);
    snmp_sess_read(m_sessp, &fdset);
}

// ── PDU callback (file-static) ───────────────────────────────────────────
// Called by net-snmp on the owner thread (inside snmp_sess_read) for each
// successfully authenticated PDU.

static int trapPduCallback(int operation,
                           netsnmp_session* /*sess*/,
                           int /*reqid*/,
                           netsnmp_pdu* pdu,
                           void* magic)
{
    if (operation != NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE || !pdu || !magic)
        return 1;

    // Only process TRAP and INFORM PDUs.
    if (pdu->command != SNMP_MSG_TRAP2 && pdu->command != SNMP_MSG_INFORM)
        return 1;

    auto* self = static_cast<SnmpTrapReceiver*>(magic);

    SnmpTrapInfo info;

    // ── Source address ────────────────────────────────────────────────────
    // For SNMPv3 the agent address is in the transport opaque data, which
    // net-snmp formats as "IP:port". We fall back to agent_addr for v1.
    if (pdu->transport_data && pdu->transport_data_length >= 6) {
        // transport_data for UDP: 4 bytes addr + 2 bytes port (network order)
        const auto* src = static_cast<const unsigned char*>(pdu->transport_data);
        char ipbuf[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, src, ipbuf, sizeof(ipbuf)))
            info.sourceAddress = ipbuf;
    }

    // ── Security info ─────────────────────────────────────────────────────
    if (pdu->securityName && pdu->securityNameLen > 0)
        info.securityName.assign(pdu->securityName, pdu->securityNameLen);

    switch (pdu->securityLevel) {
        case SNMP_SEC_LEVEL_AUTHPRIV:
            info.securityLevel = SnmpSecurityLevel::AuthPriv; break;
        case SNMP_SEC_LEVEL_AUTHNOPRIV:
            info.securityLevel = SnmpSecurityLevel::AuthNoPriv; break;
        default:
            info.securityLevel = SnmpSecurityLevel::NoAuthNoPriv; break;
    }

    if (pdu->contextName && pdu->contextNameLen > 0)
        info.contextName.assign(pdu->contextName, pdu->contextNameLen);

    // ── Extract trapOID + varbinds ────────────────────────────────────────
    // Per RFC 3416, SNMPv2-TRAP and INFORM PDUs carry:
    //   varbind[0] = sysUpTime.0   (TimeTicks)
    //   varbind[1] = snmpTrapOID.0 (OID value)
    //   varbind[2..] = user varbinds
    netsnmp_variable_list* var = pdu->variables;
    for (; var; var = var->next_variable) {
        const bool isSysUpTime =
            (var->name_length == k_sysUpTimeLen) &&
            (memcmp(var->name, k_sysUpTime, k_sysUpTimeLen * sizeof(oid)) == 0);

        const bool isTrapOid =
            (var->name_length == k_snmpTrapOIDLen) &&
            (memcmp(var->name, k_snmpTrapOID, k_snmpTrapOIDLen * sizeof(oid)) == 0);

        if (isSysUpTime)
            continue; // skip — not part of the payload

        if (isTrapOid) {
            if (var->type == ASN_OBJECT_ID && var->val.objid) {
                std::vector<std::uint32_t> comps(
                    var->val.objid,
                    var->val.objid + var->val_len / sizeof(oid));
                info.trapOid = SnmpOid(std::move(comps));
            }
            continue; // parsed into dedicated field, not varbinds
        }

        // All other varbinds are user payload.
        auto vbs = parseVarBinds(var);
        for (auto& vb : vbs)
            info.varbinds.push_back(std::move(vb));
        break; // parseVarBinds already walks the chain; stop our outer loop
    }

    self->trapReceived.emit(std::move(info));

    // Return 1 to acknowledge INFORM PDUs (net-snmp sends the response for us).
    return 1;
}

} // namespace snf
