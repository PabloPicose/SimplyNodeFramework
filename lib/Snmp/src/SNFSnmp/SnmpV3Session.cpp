/**
 * @file SnmpV3Session.cpp
 * @ingroup SNFSnmp
 *
 * Implementation notes
 * ────────────────────
 * net-snmp's SNMP_VERSION_3 + USM is used for all operations.
 *
 * Threading model
 * ───────────────
 * Each public operation (get/set/walk) captures the current config and
 * credentials by value, then spawns a detached std::thread that:
 *   1. Opens a fresh net-snmp session.
 *   2. Performs the synchronous SNMP operation (snmp_synch_response /
 *      repeated GETNEXT for walk).
 *   3. Posts the result back to the caller's EventLoop via loop->post().
 *
 * A fresh session is opened per operation to avoid shared-state concurrency
 * issues inside net-snmp. The one-time engine-ID discovery overhead can be
 * eliminated by supplying SnmpV3Config::contextEngineId.
 *
 * init_snmp() global initialisation
 * ──────────────────────────────────
 * init_snmp() must be called exactly once per process before any session is
 * opened. This is handled with std::call_once in snmpGlobalInit().
 */

// net-snmp MUST come before any system headers on some platforms.
// net-snmp-config.h must be first.
#include <net-snmp/net-snmp-config.h>   // NOLINT
#include <net-snmp/net-snmp-includes.h> // NOLINT

// Some net-snmp distributions define NETSNMP_DRAFT_BLUMENTHAL_AES_04 but omit
// the corresponding _LEN constants. Provide fallbacks derived from the actual
// OID array sizes declared in the headers.
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

#include "SNFSnmp/SnmpV3Session.h"

#include <SNFCore/EventLoop.h>
#include <SNFCore/NodePtr.h>

#include <arpa/inet.h>

#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace snf {

// ── global init ───────────────────────────────────────────────────────────

namespace {

std::once_flag g_snmpInitFlag;

void snmpGlobalInit()
{
    std::call_once(g_snmpInitFlag, [] {
        init_snmp("SNFSnmp");
    });
}

// ── Auth protocol helpers ─────────────────────────────────────────────────

struct AuthInfo {
    const oid*  proto;
    std::size_t protoLen;
};

AuthInfo authInfoFor(SnmpAuthProtocol p)
{
    switch (p) {
        case SnmpAuthProtocol::MD5:
            return { usmHMACMD5AuthProtocol,  USM_AUTH_PROTO_MD5_LEN  };
        case SnmpAuthProtocol::SHA1:
            return { usmHMACSHA1AuthProtocol, USM_AUTH_PROTO_SHA_LEN  };
        case SnmpAuthProtocol::SHA256:
#ifdef NETSNMP_DRAFT_BLUMENTHAL_AES_04
            return { usmHMAC192SHA256AuthProtocol, USM_AUTH_PROTO_SHA256_LEN };
#else
            return { usmHMACSHA1AuthProtocol, USM_AUTH_PROTO_SHA_LEN  }; // fallback
#endif
        case SnmpAuthProtocol::SHA512:
#ifdef NETSNMP_DRAFT_BLUMENTHAL_AES_04
            return { usmHMAC384SHA512AuthProtocol, USM_AUTH_PROTO_SHA512_LEN };
#else
            return { usmHMACSHA1AuthProtocol, USM_AUTH_PROTO_SHA_LEN  }; // fallback
#endif
        default:
            return { usmNoAuthProtocol, USM_AUTH_PROTO_NOAUTH_LEN };
    }
}

struct PrivInfo {
    const oid*  proto;
    std::size_t protoLen;
};

PrivInfo privInfoFor(SnmpPrivProtocol p)
{
    switch (p) {
        case SnmpPrivProtocol::DES:
            return { usmDESPrivProtocol,    USM_PRIV_PROTO_DES_LEN };
        case SnmpPrivProtocol::AES128:
            return { usmAESPrivProtocol,    USM_PRIV_PROTO_AES_LEN };
        case SnmpPrivProtocol::AES256:
#ifdef NETSNMP_DRAFT_BLUMENTHAL_AES_04
            return { usmAES256PrivProtocol, USM_PRIV_PROTO_AES256_LEN };
#else
            return { usmAESPrivProtocol, USM_PRIV_PROTO_AES_LEN }; // fallback
#endif
        default:
            return { usmNoPrivProtocol, USM_PRIV_PROTO_NOPRIV_LEN };
    }
}

// ── Convert SnmpOid → net-snmp oid[] ─────────────────────────────────────

std::vector<oid> toNetSnmpOid(const SnmpOid& snmpOid)
{
    const auto& comps = snmpOid.components();
    std::vector<oid> out(comps.begin(), comps.end());
    return out;
}

// ── Convert netsnmp_variable_list chain → SnmpVarBind vector ─────────────

std::vector<SnmpVarBind> parseVarBinds(netsnmp_variable_list* vars)
{
    std::vector<SnmpVarBind> result;

    for (; vars; vars = vars->next_variable) {
        SnmpVarBind vb;

        // OID
        std::vector<std::uint32_t> oidComps(vars->name,
                                             vars->name + vars->name_length);
        vb.oid = SnmpOid(std::move(oidComps));

        // Value
        switch (vars->type) {
            case ASN_INTEGER:
                vb.value = SnmpValue::fromInteger32(
                    static_cast<std::int32_t>(*vars->val.integer));
                break;

            case ASN_COUNTER:
                vb.value = SnmpValue::fromCounter32(
                    static_cast<std::uint32_t>(*vars->val.integer));
                break;

            case ASN_GAUGE:
                vb.value = SnmpValue::fromGauge32(
                    static_cast<std::uint32_t>(*vars->val.integer));
                break;

            case ASN_TIMETICKS:
                vb.value = SnmpValue::fromTimeTicks(
                    static_cast<std::uint32_t>(*vars->val.integer));
                break;

            case ASN_COUNTER64: {
                std::uint64_t v =
                    (static_cast<std::uint64_t>(vars->val.counter64->high) << 32) |
                     static_cast<std::uint64_t>(vars->val.counter64->low);
                vb.value = SnmpValue::fromCounter64(v);
                break;
            }

            case ASN_OCTET_STR: {
                std::vector<std::uint8_t> bytes(
                    vars->val.string,
                    vars->val.string + vars->val_len);
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
                std::vector<std::uint8_t> bytes(
                    vars->val.string,
                    vars->val.string + vars->val_len);
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
                // Unknown / error indicator — leave value as Null.
                break;
        }

        result.push_back(std::move(vb));
    }

    return result;
}

// ── Open a configured SNMPv3 session ─────────────────────────────────────

/**
 * Opens a net-snmp session configured for SNMPv3/USM.
 * Returns the session handle on success, or nullptr on failure (details in errOut).
 * The caller must call snmp_close() on the returned handle when done.
 */
netsnmp_session* openSession(const SnmpV3Config& cfg,
                              const SnmpV3Credentials& creds,
                              std::string& errOut)
{
    snmpGlobalInit();

    netsnmp_session tmpl = {};
    snmp_sess_init(&tmpl);

    tmpl.version  = SNMP_VERSION_3;
    tmpl.retries  = cfg.retries;
    tmpl.timeout  = static_cast<long>(
        std::chrono::duration_cast<std::chrono::microseconds>(cfg.timeout).count());

    // Peer name: "udp:<host>:<port>"
    const std::string peerName =
        "udp:" + cfg.host + ":" + std::to_string(cfg.port);
    tmpl.peername = const_cast<char*>(peerName.c_str());

    // Security name
    tmpl.securityName    = const_cast<char*>(creds.username.c_str());
    tmpl.securityNameLen = creds.username.size();

    // Security level
    switch (creds.securityLevel) {
        case SnmpSecurityLevel::NoAuthNoPriv:
            tmpl.securityLevel = SNMP_SEC_LEVEL_NOAUTH; break;
        case SnmpSecurityLevel::AuthNoPriv:
            tmpl.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV; break;
        case SnmpSecurityLevel::AuthPriv:
            tmpl.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV; break;
    }

    // Auth
    if (creds.securityLevel != SnmpSecurityLevel::NoAuthNoPriv &&
        creds.authProtocol  != SnmpAuthProtocol::None) {
        const auto auth = authInfoFor(creds.authProtocol);
        tmpl.securityAuthProto    = const_cast<oid*>(auth.proto);
        tmpl.securityAuthProtoLen = auth.protoLen;
        tmpl.securityAuthKeyLen   = USM_AUTH_KU_LEN;

        const int rc = generate_Ku(
            tmpl.securityAuthProto,
            static_cast<u_int>(tmpl.securityAuthProtoLen),
            reinterpret_cast<u_char*>(
                const_cast<char*>(creds.authPassphrase.c_str())),
            creds.authPassphrase.size(),
            tmpl.securityAuthKey,
            &tmpl.securityAuthKeyLen);

        if (rc != SNMPERR_SUCCESS) {
            errOut = "Failed to generate authentication key (generate_Ku)";
            return nullptr;
        }
    }

    // Priv
    if (creds.securityLevel == SnmpSecurityLevel::AuthPriv &&
        creds.privProtocol  != SnmpPrivProtocol::None) {
        const auto priv = privInfoFor(creds.privProtocol);
        tmpl.securityPrivProto    = const_cast<oid*>(priv.proto);
        tmpl.securityPrivProtoLen = priv.protoLen;
        tmpl.securityPrivKeyLen   = USM_PRIV_KU_LEN;

        // The privacy key is derived using the auth hash algorithm.
        const auto auth = authInfoFor(creds.authProtocol);
        const int rc = generate_Ku(
            const_cast<oid*>(auth.proto),
            static_cast<u_int>(auth.protoLen),
            reinterpret_cast<u_char*>(
                const_cast<char*>(creds.privPassphrase.c_str())),
            creds.privPassphrase.size(),
            tmpl.securityPrivKey,
            &tmpl.securityPrivKeyLen);

        if (rc != SNMPERR_SUCCESS) {
            errOut = "Failed to generate privacy key (generate_Ku)";
            return nullptr;
        }
    }

    // Context (optional)
    if (!cfg.contextName.empty()) {
        tmpl.contextName    = const_cast<char*>(cfg.contextName.c_str());
        tmpl.contextNameLen = cfg.contextName.size();
    }

    netsnmp_session* ss = snmp_open(&tmpl);
    if (!ss) {
        char* errstr = nullptr;
        int   liberr = 0, syserr = 0;
        snmp_error(&tmpl, &liberr, &syserr, &errstr);
        if (errstr) {
            errOut = errstr;
            SNMP_FREE(errstr);
        } else {
            errOut = "snmp_open() failed (unknown error)";
        }
        return nullptr;
    }

    return ss;
}

} // anonymous namespace

// ── SnmpV3Session ─────────────────────────────────────────────────────────

SnmpV3Session::SnmpV3Session(Node* parent)
    : Node(parent)
{
}

SnmpV3Session::~SnmpV3Session() = default;

// ── Configuration ─────────────────────────────────────────────────────────

void SnmpV3Session::setConfig(const SnmpV3Config& config)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

SnmpV3Config SnmpV3Session::config() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void SnmpV3Session::setCredentials(const SnmpV3Credentials& credentials)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_credentials = credentials;
}

SnmpV3Credentials SnmpV3Session::credentials() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_credentials;
}

// ── Operations ────────────────────────────────────────────────────────────

void SnmpV3Session::get(const SnmpOid& oid)
{
    get(std::vector<SnmpOid>{oid});
}

void SnmpV3Session::get(const std::vector<SnmpOid>& oids)
{
    SnmpV3Config      cfg;
    SnmpV3Credentials creds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cfg   = m_config;
        creds = m_credentials;
    }

    NodePtr<SnmpV3Session> self(this);

    std::thread([self, oids, cfg, creds]() mutable {
        std::string errStr;
        netsnmp_session* ss = openSession(cfg, creds, errStr);
        if (!ss) {
            if (self && !self.isMarkedToDelete()) {
                if (EventLoop* loop = self->ownerEventLoop())
                    loop->post([self, errStr = std::move(errStr)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->errorOccurred.emit(std::move(errStr));
                    });
            }
            return;
        }

        netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GET);
        for (const auto& snmpOid : oids) {
            auto netOid = toNetSnmpOid(snmpOid);
            snmp_add_null_var(pdu, netOid.data(), netOid.size());
        }

        netsnmp_pdu* response = nullptr;
        const int status = snmp_synch_response(ss, pdu, &response);

        std::vector<SnmpVarBind> varbinds;
        std::string              error;

        if (status == STAT_SUCCESS) {
            if (response->errstat == SNMP_ERR_NOERROR) {
                varbinds = parseVarBinds(response->variables);
            } else {
                error = "SNMP agent error: ";
                error += snmp_errstring(static_cast<int>(response->errstat));
            }
        } else if (status == STAT_TIMEOUT) {
            error = "Request timed out";
        } else {
            char* errstr = nullptr;
            int   liberr = 0, syserr = 0;
            snmp_sess_error(ss, &liberr, &syserr, &errstr);
            error = errstr ? std::string(errstr) : "Unknown SNMP error";
            SNMP_FREE(errstr);
        }

        if (response)
            snmp_free_pdu(response);
        snmp_close(ss);

        if (self && !self.isMarkedToDelete()) {
            if (EventLoop* loop = self->ownerEventLoop()) {
                if (error.empty()) {
                    loop->post([self, varbinds = std::move(varbinds)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->getResult.emit(std::move(varbinds));
                    });
                } else {
                    loop->post([self, error = std::move(error)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->errorOccurred.emit(std::move(error));
                    });
                }
            }
        }
    }).detach();
}

void SnmpV3Session::set(const SnmpVarBind& varBind)
{
    set(std::vector<SnmpVarBind>{varBind});
}

void SnmpV3Session::set(const std::vector<SnmpVarBind>& varBinds)
{
    SnmpV3Config      cfg;
    SnmpV3Credentials creds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cfg   = m_config;
        creds = m_credentials;
    }

    NodePtr<SnmpV3Session> self(this);

    std::thread([self, varBinds, cfg, creds]() mutable {
        std::string errStr;
        netsnmp_session* ss = openSession(cfg, creds, errStr);
        if (!ss) {
            if (self && !self.isMarkedToDelete()) {
                if (EventLoop* loop = self->ownerEventLoop())
                    loop->post([self, errStr = std::move(errStr)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->errorOccurred.emit(std::move(errStr));
                    });
            }
            return;
        }

        netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_SET);
        for (const auto& vb : varBinds) {
            auto netOid    = toNetSnmpOid(vb.oid);
            const char type = vb.value.netSnmpTypeChar();
            const std::string valStr = vb.value.toNetSnmpString();

            const int rc = snmp_add_var(pdu,
                netOid.data(), netOid.size(),
                type, valStr.c_str());

            if (rc != 0) {
                snmp_free_pdu(pdu);
                snmp_close(ss);
                const std::string error =
                    "Failed to encode varbind for OID " + vb.oid.toString();
                if (self && !self.isMarkedToDelete()) {
                    if (EventLoop* loop = self->ownerEventLoop())
                        loop->post([self, error]() mutable {
                            if (self && !self.isMarkedToDelete())
                                self->errorOccurred.emit(error);
                        });
                }
                return;
            }
        }

        netsnmp_pdu* response = nullptr;
        const int status = snmp_synch_response(ss, pdu, &response);

        std::string error;

        if (status == STAT_SUCCESS) {
            if (response->errstat != SNMP_ERR_NOERROR) {
                error = "SNMP agent error: ";
                error += snmp_errstring(static_cast<int>(response->errstat));
            }
        } else if (status == STAT_TIMEOUT) {
            error = "Request timed out";
        } else {
            char* errstr = nullptr;
            int   liberr = 0, syserr = 0;
            snmp_sess_error(ss, &liberr, &syserr, &errstr);
            error = errstr ? std::string(errstr) : "Unknown SNMP error";
            SNMP_FREE(errstr);
        }

        if (response)
            snmp_free_pdu(response);
        snmp_close(ss);

        if (self && !self.isMarkedToDelete()) {
            if (EventLoop* loop = self->ownerEventLoop()) {
                if (error.empty()) {
                    loop->post([self]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->setResult.emit();
                    });
                } else {
                    loop->post([self, error = std::move(error)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->errorOccurred.emit(std::move(error));
                    });
                }
            }
        }
    }).detach();
}

void SnmpV3Session::walk(const SnmpOid& rootOid)
{
    SnmpV3Config      cfg;
    SnmpV3Credentials creds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cfg   = m_config;
        creds = m_credentials;
    }

    NodePtr<SnmpV3Session> self(this);

    std::thread([self, rootOid, cfg, creds]() mutable {
        std::string errStr;
        netsnmp_session* ss = openSession(cfg, creds, errStr);
        if (!ss) {
            if (self && !self.isMarkedToDelete()) {
                if (EventLoop* loop = self->ownerEventLoop())
                    loop->post([self, errStr = std::move(errStr)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->errorOccurred.emit(std::move(errStr));
                    });
            }
            return;
        }

        std::vector<SnmpVarBind> allVarbinds;
        std::string              error;

        // Current OID cursor starts at the root.
        std::vector<oid> currentOid = toNetSnmpOid(rootOid);

        while (true) {
            netsnmp_pdu* pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
            snmp_add_null_var(pdu, currentOid.data(), currentOid.size());

            netsnmp_pdu* response = nullptr;
            const int status = snmp_synch_response(ss, pdu, &response);

            if (status == STAT_SUCCESS) {
                if (response->errstat != SNMP_ERR_NOERROR) {
                    // errstat == SNMP_ERR_NOSUCHNAME means end of MIB.
                    if (response->errstat != SNMP_ERR_NOSUCHNAME) {
                        error = "SNMP agent error: ";
                        error += snmp_errstring(static_cast<int>(response->errstat));
                    }
                    snmp_free_pdu(response);
                    break;
                }

                netsnmp_variable_list* var = response->variables;
                if (!var) {
                    snmp_free_pdu(response);
                    break;
                }

                // endOfMibView exception from the agent.
                if (var->type == SNMP_ENDOFMIBVIEW ||
                    var->type == SNMP_NOSUCHOBJECT  ||
                    var->type == SNMP_NOSUCHINSTANCE) {
                    snmp_free_pdu(response);
                    break;
                }

                // Check if the returned OID is still under rootOid.
                std::vector<std::uint32_t> returnedComps(var->name,
                                                          var->name + var->name_length);
                SnmpOid returnedOid(std::move(returnedComps));

                if (!rootOid.isPrefixOf(returnedOid)) {
                    snmp_free_pdu(response);
                    break;
                }

                auto vbs = parseVarBinds(var);
                for (auto& vb : vbs)
                    allVarbinds.push_back(std::move(vb));

                // Advance cursor to the last returned OID.
                currentOid.assign(var->name, var->name + var->name_length);
                snmp_free_pdu(response);

            } else if (status == STAT_TIMEOUT) {
                error = "Request timed out during walk";
                if (response) snmp_free_pdu(response);
                break;
            } else {
                char* errstr = nullptr;
                int   liberr = 0, syserr = 0;
                snmp_sess_error(ss, &liberr, &syserr, &errstr);
                error = errstr ? std::string(errstr) : "Unknown SNMP error";
                SNMP_FREE(errstr);
                if (response) snmp_free_pdu(response);
                break;
            }
        }

        snmp_close(ss);

        if (self && !self.isMarkedToDelete()) {
            if (EventLoop* loop = self->ownerEventLoop()) {
                if (error.empty()) {
                    loop->post([self, vbs = std::move(allVarbinds)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->walkResult.emit(std::move(vbs));
                    });
                } else {
                    loop->post([self, error = std::move(error)]() mutable {
                        if (self && !self.isMarkedToDelete())
                            self->errorOccurred.emit(std::move(error));
                    });
                }
            }
        }
    }).detach();
}

} // namespace snf
