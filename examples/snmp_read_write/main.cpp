/**
 * @file main.cpp
 * @brief SNMPv3 read/write example.
 *
 * This example demonstrates how to use SNFSnmp to:
 *   1. Read (GET) an OID from a remote SNMP agent.
 *   2. Write (SET) a new value to that same OID.
 *
 * ── What you need to configure ───────────────────────────────────────────────
 *
 * Edit the constants in the "Configuration" section below to match your
 * SNMP agent:
 *
 *   AGENT_HOST      — IP address or hostname of the SNMP agent.
 *   AGENT_PORT      — UDP port (default 161).
 *   USERNAME        — SNMPv3 USM username configured on the agent.
 *   AUTH_PASSPHRASE — Authentication passphrase (≥ 8 characters).
 *   PRIV_PASSPHRASE — Privacy (encryption) passphrase (≥ 8 characters).
 *   TARGET_OID      — OID to read and then write.
 *   NEW_VALUE       — Integer32 value to write.
 *
 * ── Testing locally with snmpd ────────────────────────────────────────────────
 *
 * You can create a local agent for testing. Add this to /etc/snmp/snmpd.conf:
 *
 *   createUser snmpAdmin SHA "authSecret123!" AES "privSecret123!"
 *   rwuser     snmpAdmin priv
 *
 * Then restart: sudo systemctl restart snmpd
 *
 * A safe writable test OID on most agents is:
 *   1.3.6.1.2.1.1.6.0  (sysLocation.0, OctetString)
 *
 * For Integer32 writes use a MIB-II extension OID on your target device.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFSnmp/SnmpOid.h>
#include <SNFSnmp/SnmpTypes.h>
#include <SNFSnmp/SnmpV3Config.h>
#include <SNFSnmp/SnmpV3Credentials.h>
#include <SNFSnmp/SnmpV3Session.h>
#include <SNFSnmp/SnmpVarBind.h>
#include <SNFSnmp/SnmpValue.h>

#include <chrono>
#include <iostream>

using namespace snf;
using namespace std::chrono_literals;

// ── Configuration — edit these to match your agent ───────────────────────────

static constexpr const char* AGENT_HOST      = "127.0.0.1";
static constexpr uint16_t    AGENT_PORT      = 161;

static constexpr const char* USERNAME        = "testuser";
static constexpr const char* AUTH_PASSPHRASE = "authpass";
static constexpr const char* PRIV_PASSPHRASE = "privpass";

// OID to read and then overwrite. The example uses sysLocation.0 which is a
// writable OctetString present on virtually every Net-SNMP agent.
static constexpr const char* TARGET_OID      = "1.3.6.1.4.1.99999.1.2.4.0";

// New value to write. Change SnmpValue::fromOctetString to match your OID type.
static const SnmpValue NEW_VALUE = SnmpValue::fromOctetString("2");

// ── Example state machine ─────────────────────────────────────────────────────

namespace {

/**
 * Simple two-step state machine:
 *   Step 1 — GET the current value of TARGET_OID and print it.
 *   Step 2 — SET the new value, then stop the application.
 */
enum class Step { Get, Set, Done };

Step g_step = Step::Get;

void printResult(const std::vector<SnmpVarBind>& vbs)
{
    for (const auto& vb : vbs)
        std::cout << "  " << vb.oid.toString()
                  << "  =  " << vb.value.toDisplayString() << '\n';
}

} // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    Application app(0, nullptr);

    // ── Build config ──────────────────────────────────────────────────────────

    SnmpV3Config cfg;
    cfg.host    = AGENT_HOST;
    cfg.port    = AGENT_PORT;
    cfg.timeout = 5s;
    cfg.retries = 2;

    SnmpV3Credentials creds;
    creds.username       = USERNAME;
    creds.securityLevel  = SnmpSecurityLevel::AuthPriv;
    creds.authProtocol   = SnmpAuthProtocol::SHA1;
    creds.authPassphrase = AUTH_PASSPHRASE;
    creds.privProtocol   = SnmpPrivProtocol::AES128;
    creds.privPassphrase = PRIV_PASSPHRASE;

    const SnmpOid targetOid(TARGET_OID);

    // ── Create session ────────────────────────────────────────────────────────

    auto* session = new SnmpV3Session();
    session->setConfig(cfg);
    session->setCredentials(creds);

    // ── Wire up signals ───────────────────────────────────────────────────────

    // GET result → print current value, then issue SET.
    session->getResult.connect([session, targetOid](const std::vector<SnmpVarBind>& vbs) {
        std::cout << "\n[GET result]\n";
        printResult(vbs);

        std::cout << "\nSetting new value: " << NEW_VALUE.toDisplayString() << '\n';

        SnmpVarBind vb;
        vb.oid   = targetOid;
        vb.value = NEW_VALUE;
        session->set(vb);
    });

    // SET result → confirm success, then stop.
    session->setResult.connect([session]() {
        std::cout << "\n[SET result] Write successful.\n";

        // Read back to confirm.
        session->getResult.connect([](const std::vector<SnmpVarBind>& vbs) {
            std::cout << "\n[Read-back]\n";
            printResult(vbs);

            // Stop the event loop — we are done.
            if (EventLoop* loop = Application::instance()->getOrCreateCurrentThreadEventLoop())
                loop->post([loop]() { loop->stop(); });
        });

        // Re-use the same OID we just wrote.
        const SnmpOid targetOid(TARGET_OID);
        session->get(targetOid);
    });

    // Any error → print and stop.
    session->errorOccurred.connect([](const std::string& error) {
        std::cerr << "\n[SNMP error] " << error << '\n';

        if (EventLoop* loop = Application::instance()->getOrCreateCurrentThreadEventLoop())
            loop->post([loop]() { loop->stop(); });
    });

    // ── Safety timeout (10 s) ─────────────────────────────────────────────────

    auto* watchdog = new Timer();
    watchdog->setSingleShot(true);
    watchdog->timeout.connect([]() {
        std::cerr << "\n[Timeout] No response within 10 seconds.\n";
        if (EventLoop* loop = Application::instance()->getOrCreateCurrentThreadEventLoop())
            loop->post([loop]() { loop->stop(); });
    });
    watchdog->start(10s);

    // ── Kick off with a GET ───────────────────────────────────────────────────

    std::cout << "Connecting to " << AGENT_HOST << ':' << AGENT_PORT
              << "  user=" << USERNAME << '\n';
    std::cout << "Reading OID: " << TARGET_OID << " ...\n";

    session->get(targetOid);

    // ── Run until done ────────────────────────────────────────────────────────

    app.run();

    std::cout << "\nDone.\n";
    return 0;
}
