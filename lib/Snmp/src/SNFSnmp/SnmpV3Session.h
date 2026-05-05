#pragma once

/**
 * @file SnmpV3Session.h
 * @brief Signal-driven SNMPv3 client for GET, SET, and WALK operations.
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpTypes.h"
#include "SNFSnmp/SnmpV3Config.h"
#include "SNFSnmp/SnmpV3Credentials.h"
#include "SNFSnmp/SnmpVarBind.h"

#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>

#include <mutex>
#include <vector>

namespace snf {

/**
 * @class SnmpV3Session
 * @ingroup SNFSnmp
 * @brief Signal-driven SNMPv3 client for GET, SET, and WALK operations.
 *
 * `SnmpV3Session` wraps the net-snmp library and exposes an asynchronous,
 * signal-based API that integrates naturally with the SNF event-loop model.
 *
 * Each operation (get / set / walk) is dispatched to a background thread.
 * The result is delivered on the session's owner thread via the corresponding
 * signal, so it is safe to update application state directly inside the
 * connected slots.
 *
 * **Lifecycle:**
 * - Construct on the heap with `new SnmpV3Session(parent)`.
 * - Call `setConfig()` and `setCredentials()` before issuing requests.
 * - Multiple in-flight requests are allowed; results arrive as each completes.
 *
 * **Notes on engine discovery:**
 * Each call to `get()`, `set()`, or `walk()` opens a fresh net-snmp session
 * internally. SNMPv3 engine-ID discovery (1–2 extra round-trips) is therefore
 * performed for every call. For high-frequency polling consider caching the
 * engine ID via `SnmpV3Config::contextEngineId`.
 *
 * @code
 * snf::SnmpV3Config cfg;
 * cfg.host = "192.168.10.1";
 *
 * snf::SnmpV3Credentials creds;
 * creds.username       = "snmpAdmin";
 * creds.securityLevel  = snf::SnmpSecurityLevel::AuthPriv;
 * creds.authProtocol   = snf::SnmpAuthProtocol::SHA1;
 * creds.authPassphrase = "authSecret";
 * creds.privProtocol   = snf::SnmpPrivProtocol::AES128;
 * creds.privPassphrase = "privSecret";
 *
 * auto* session = new snf::SnmpV3Session();
 * session->setConfig(cfg);
 * session->setCredentials(creds);
 *
 * session->getResult.connect([](const std::vector<snf::SnmpVarBind>& vbs) {
 *     for (const auto& vb : vbs)
 *         std::cout << vb.oid.toString() << " = " << vb.value.toDisplayString() << '\n';
 * });
 * session->errorOccurred.connect([](const std::string& msg) {
 *     std::cerr << "SNMP error: " << msg << '\n';
 * });
 *
 * // Read sysDescr.0
 * session->get(snf::SnmpOid("1.3.6.1.2.1.1.1.0"));
 *
 * // Set a custom OID
 * snf::SnmpVarBind vb;
 * vb.oid   = snf::SnmpOid("1.3.6.1.4.1.12345.1.1.0");
 * vb.value = snf::SnmpValue::fromInteger32(134800);
 * session->set(vb);
 *
 * // Walk the interfaces table
 * session->walk(snf::SnmpOid("1.3.6.1.2.1.2.2"));
 * @endcode
 */
class SnmpV3Session : public Node
{
public:
    explicit SnmpV3Session(Node* parent = nullptr);
    ~SnmpV3Session() override;

    // ── Configuration ─────────────────────────────────────────────────────

    /**
     * @brief Sets the target agent address and transport parameters.
     *
     * Thread-safe: may be called before or between operations, but not
     * while a request is already in flight for that session.
     */
    void setConfig(const SnmpV3Config& config);

    /** @brief Returns the current target configuration. */
    SnmpV3Config config() const;

    /**
     * @brief Sets the USM security credentials.
     *
     * Must be configured before any operation is issued.
     */
    void setCredentials(const SnmpV3Credentials& credentials);

    /** @brief Returns the current credentials. */
    SnmpV3Credentials credentials() const;

    // ── Operations ────────────────────────────────────────────────────────

    /**
     * @brief Sends a GET request for a single OID.
     *
     * On success, `getResult` is emitted with a one-element vector.
     * On failure, `errorOccurred` is emitted with a description.
     */
    void get(const SnmpOid& oid);

    /**
     * @brief Sends a single GET request for multiple OIDs (GetRequest PDU).
     *
     * On success, `getResult` is emitted once with all varbinds.
     */
    void get(const std::vector<SnmpOid>& oids);

    /**
     * @brief Sends a SET request for a single variable binding.
     *
     * On success, `setResult` is emitted.
     * On failure, `errorOccurred` is emitted.
     */
    void set(const SnmpVarBind& varBind);

    /**
     * @brief Sends a single SET request for multiple variable bindings.
     *
     * On success, `setResult` is emitted once.
     */
    void set(const std::vector<SnmpVarBind>& varBinds);

    /**
     * @brief Walks the subtree rooted at @p rootOid via repeated GETNEXT requests.
     *
     * The walk stops when the returned OID is no longer a descendant of
     * @p rootOid or when an @c endOfMibView exception is encountered.
     *
     * On success, `walkResult` is emitted with all collected varbinds.
     * On failure, `errorOccurred` is emitted.
     */
    void walk(const SnmpOid& rootOid);

    // ── Signals ───────────────────────────────────────────────────────────

    /** @brief Emitted on the owner thread when a GET operation succeeds. */
    Signal<std::vector<SnmpVarBind>> getResult;

    /** @brief Emitted on the owner thread when a SET operation succeeds. */
    Signal<>                          setResult;

    /** @brief Emitted on the owner thread when a WALK operation succeeds. */
    Signal<std::vector<SnmpVarBind>> walkResult;

    /**
     * @brief Emitted on the owner thread when any operation fails.
     * @param error Human-readable description of the failure.
     */
    Signal<std::string> errorOccurred;

    void update() override {}

private:
    mutable std::mutex m_mutex;
    SnmpV3Config       m_config;
    SnmpV3Credentials  m_credentials;
};

} // namespace snf
