/**
 * @file main.cpp
 * @brief Concurrent SNMP GET/SET + TRAP reception example.
 *
 * This example shows how a single event loop can issue SNMPv3 GET/SET
 * requests AND receive SNMPv3 TRAPs simultaneously, with no extra threads.
 *
 * The sequence is:
 *   1. Start a SnmpTrapReceiver on the configured trap port.
 *   2. Issue a GET request for TARGET_OID.
 *   3. While waiting for the GET response, any agent traps will be printed
 *      immediately on the same thread by the event loop.
 *   4. On GET success, issue a SET to update the value.
 *   5. While waiting for the SET response, continue listening for traps.
 *   6. On SET success, read back the value and stop.
 *
 * ── What you need to configure ───────────────────────────────────────────────
 *
 *   AGENT_HOST        — IP of the SNMP agent (GET/SET target).
 *   AGENT_PORT        — UDP port of the agent (default 161).
 *   TRAP_PORT         — UDP port to listen for inbound traps (default 1162).
 *                       Use 162 if running as root / with authbind.
 *   USERNAME          — SNMPv3 USM username configured on the agent.
 *   AUTH_PASSPHRASE   — Authentication passphrase (≥ 8 characters).
 *   PRIV_PASSPHRASE   — Privacy passphrase (≥ 8 characters).
 *   TARGET_OID        — OID to GET and then SET.
 *   NEW_VALUE         — Value to write on the SET step.
 *
 * ── Testing locally ───────────────────────────────────────────────────────────
 *
 * Use the snmp_agent_sim.py pass-persist script at /opt/snmp_agent_sim.py
 * together with an snmpd.conf that has:
 *
 *   createUser testuser SHA "authpass" AES "privpass"
 *   rwuser     testuser priv
 *
 * To generate a test trap from the command line:
 *
 *   snmptrap -v3 -l authPriv -u testuser \
 *            -a SHA  -A authpass \
 *            -x AES  -X privpass \
 *            -e 0x800000000102030405 \
 *            localhost:1162 '' \
 *            1.3.6.1.4.1.99999.1.0 \
 *            1.3.6.1.4.1.99999.1.1.0 i 42
 *
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFSnmp/SnmpOid.h>
#include <SNFSnmp/SnmpTrapInfo.h>
#include <SNFSnmp/SnmpTrapReceiver.h>
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

// ── Configuration — edit these to match your setup ───────────────────────────

static constexpr const char* AGENT_HOST      = "127.0.0.1";
static constexpr uint16_t    AGENT_PORT      = 161;

// Port to listen for inbound traps.
// Ports below 1024 require root or CAP_NET_BIND_SERVICE / authbind.
static constexpr uint16_t    TRAP_PORT       = 1162;

static constexpr const char* USERNAME        = "testuser";
static constexpr const char* AUTH_PASSPHRASE = "authpass";
static constexpr const char* PRIV_PASSPHRASE = "privpass";

// OID to read (GET) and then overwrite (SET).
static constexpr const char* TARGET_OID      = "1.3.6.1.4.1.99999.1.2.4.0";

// Value to write on the SET step.
static const SnmpValue NEW_VALUE = SnmpValue::fromOctetString("hello-from-snf");

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

void stopLoop()
{
    if (EventLoop* loop =
            Application::instance()->getOrCreateCurrentThreadEventLoop())
        loop->post([loop]() { loop->stop(); });
}

void printVarbinds(const std::vector<SnmpVarBind>& vbs)
{
    for (const auto& vb : vbs)
        std::cout << "    " << vb.oid.toString()
                  << "  =  " << vb.value.toDisplayString() << '\n';
}

} // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    Application app(0, nullptr);

    // ── Shared credentials (same user for GET/SET and for trap authentication)

    SnmpV3Credentials creds;
    creds.username       = USERNAME;
    creds.securityLevel  = SnmpSecurityLevel::AuthPriv;
    creds.authProtocol   = SnmpAuthProtocol::SHA1;
    creds.authPassphrase = AUTH_PASSPHRASE;
    creds.privProtocol   = SnmpPrivProtocol::AES128;
    creds.privPassphrase = PRIV_PASSPHRASE;

    // ── TRAP receiver ─────────────────────────────────────────────────────────
    //
    // SnmpTrapReceiver registers a file-descriptor watch in the event loop, so
    // traps are delivered on the same thread and the same iteration as all other
    // events — no locks, no races.

    auto* receiver = new SnmpTrapReceiver();
    receiver->setListenPort(TRAP_PORT);
    receiver->addV3User(creds);

    receiver->trapReceived.connect([](const SnmpTrapInfo& trap) {
        std::cout << "\n[TRAP received]\n"
                  << "  source       : " << trap.sourceAddress << '\n'
                  << "  trapOID      : " << trap.trapOid.toString() << '\n'
                  << "  securityName : " << trap.securityName << '\n'
                  << "  contextName  : " << (trap.contextName.empty()
                                              ? "(default)" : trap.contextName) << '\n';
        if (!trap.varbinds.empty()) {
            std::cout << "  varbinds:\n";
            printVarbinds(trap.varbinds);
        }
        std::cout << '\n';
    });

    receiver->errorOccurred.connect([](const std::string& err) {
        std::cerr << "[Trap receiver error] " << err << '\n';
    });

    if (!receiver->start()) {
        std::cerr << "Failed to start trap receiver on port " << TRAP_PORT << '\n';
        // Not fatal — continue without trap reception.
    } else {
        std::cout << "Listening for SNMPv3 traps on UDP port " << TRAP_PORT << '\n';
    }

    // ── GET / SET session ─────────────────────────────────────────────────────

    SnmpV3Config cfg;
    cfg.host    = AGENT_HOST;
    cfg.port    = AGENT_PORT;
    cfg.timeout = 5s;
    cfg.retries = 1;

    const SnmpOid targetOid(TARGET_OID);

    auto* session = new SnmpV3Session();
    session->setConfig(cfg);
    session->setCredentials(creds);

    // GET → print current value, then issue SET.
    session->getResult.connect([session, targetOid](const std::vector<SnmpVarBind>& vbs) {
        std::cout << "\n[GET result]\n";
        printVarbinds(vbs);

        std::cout << "Setting new value: " << NEW_VALUE.toDisplayString() << " ...\n";

        SnmpVarBind vb;
        vb.oid   = targetOid;
        vb.value = NEW_VALUE;
        session->set(vb);
    });

    // SET → read back to confirm, then stop.
    session->setResult.connect([session, targetOid]() {
        std::cout << "\n[SET result] Write successful.\n"
                  << "Reading back to confirm ...\n";

        // Re-connect getResult for the read-back step.
        session->getResult.connect([](const std::vector<SnmpVarBind>& vbs) {
            std::cout << "\n[Read-back]\n";
            printVarbinds(vbs);
            std::cout << "\nDone — stopping.\n";
            stopLoop();
        });

        session->get(targetOid);
    });

    session->errorOccurred.connect([](const std::string& error) {
        std::cerr << "\n[SNMP error] " << error << '\n';
        stopLoop();
    });

    // ── Safety timeout (30 s) ─────────────────────────────────────────────────
    //
    // A long timeout allows time to send test traps manually while the GET/SET
    // is in flight so you can observe concurrent delivery.

    auto* watchdog = new Timer();
    watchdog->setSingleShot(true);
    watchdog->timeout.connect([]() {
        std::cerr << "\n[Timeout] No response within 30 seconds.\n";
        stopLoop();
    });
    watchdog->start(30s);

    // ── Kick off sequence ─────────────────────────────────────────────────────

    std::cout << "GET/SET target : " << AGENT_HOST << ':' << AGENT_PORT
              << "  OID=" << TARGET_OID << '\n'
              << "Trap listener  : UDP port " << TRAP_PORT << '\n'
              << "User           : " << USERNAME << "\n\n"
              << "Issuing GET ...\n";

    session->get(targetOid);

    app.run();

    return 0;
}
