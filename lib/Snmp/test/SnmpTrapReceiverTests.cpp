/**
 * @file SnmpTrapReceiverTests.cpp
 * @brief Unit and integration tests for SnmpTrapReceiver.
 *
 * Test groups
 * ───────────
 * I.  Lifecycle tests (no real trap PDUs) — test socket bind/release, the
 *     isListening() state, double start/stop safety, restart after stop, and
 *     start failure when the port is already in use.
 *
 * II. Trap delivery tests (SNMP_INTEGRATION_TESTS=1, snmptrap in PATH) —
 *     send real UDP trap PDUs to the receiver and assert that trapReceived is
 *     emitted with the expected OID, varbinds, and security metadata.
 *
 *     Guard:
 *       SNMP_INTEGRATION_TESTS=1 ctest --preset native-debug -R SNFSnmpTests
 *
 *     Also require snmptrap from the net-snmp-utils package:
 *       sudo apt install snmp
 *
 * III. Concurrent GET + TRAP test (SNMP_INTEGRATION_TESTS=1) — issues a GET
 *      against the local snmpd/pass-persist agent while simultaneously sending
 *      a TRAP to the receiver; both must complete on the same event-loop
 *      thread without interfering with each other.
 */

#include <gtest/gtest.h>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFSnmp/SnmpOid.h>
#include <SNFSnmp/SnmpTrapInfo.h>
#include <SNFSnmp/SnmpTrapReceiver.h>
#include <SNFSnmp/SnmpTypes.h>
#include <SNFSnmp/SnmpV3Credentials.h>
#include <SNFSnmp/SnmpV3Session.h>
#include <SNFSnmp/SnmpVarBind.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace snf;
using namespace std::chrono_literals;

// ── Shared helpers ────────────────────────────────────────────────────────────

namespace {

// Pump the current-thread EventLoop until predicate() returns true or timeout.
bool pumpUntil(Application& app,
               const std::function<bool()>& pred,
               std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (EventLoop* loop = app.getOrCreateCurrentThreadEventLoop())
            loop->runPendingWork();
        if (pred())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    if (EventLoop* loop = app.getOrCreateCurrentThreadEventLoop())
        loop->runPendingWork();
    return pred();
}

// ── Constants ─────────────────────────────────────────────────────────────────

// High-numbered port; no root privileges required.
constexpr uint16_t TEST_TRAP_PORT = 11620;

// Credentials that match the local snmpd/pass-persist test agent.
constexpr const char* TRAP_USER      = "testuser";
constexpr const char* TRAP_AUTH_PASS = "authpass";
constexpr const char* TRAP_PRIV_PASS = "privpass";

// SNMP agent address (same instance used by SnmpV3SessionTests).
constexpr const char* AGENT_HOST = "127.0.0.1";
constexpr uint16_t    AGENT_PORT = 161;

// OIDs used in test traps.
constexpr const char* TEST_TRAP_OID     = "1.3.6.1.4.1.99999.1.0.1";
constexpr const char* TEST_VARBIND_OID  = "1.3.6.1.4.1.99999.1.1.0";

// OID to GET in the concurrent test (Integer32 in the default simulator DB).
constexpr const char* READABLE_OID = "1.3.6.1.4.1.99999.1.2.4.0";

// Fixed sender engine-ID used in snmptrap -v3 invocations.
constexpr const char* SENDER_ENGINE_ID = "0x80001f8880a3b4c5d6e700";

// Parse a hex engine-ID string ("0x80001f..." or "80001f...") into raw bytes.
std::vector<uint8_t> parseEngineId(const std::string& hexStr)
{
    const std::string hex = (hexStr.size() > 2 && hexStr.substr(0, 2) == "0x")
                            ? hexStr.substr(2) : hexStr;
    if (hex.size() % 2 != 0)
        throw std::invalid_argument("engine ID hex string has odd length");
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        std::istringstream ss(hex.substr(i, 2));
        unsigned int byte = 0;
        ss >> std::hex >> byte;
        bytes.push_back(static_cast<uint8_t>(byte));
    }
    return bytes;
}


bool integrationTestsEnabled()
{
    const char* v = std::getenv("SNMP_INTEGRATION_TESTS");
    return v && std::string(v) == "1";
}

bool snmptrapAvailable()
{
    return system("which snmptrap > /dev/null 2>&1") == 0;
}

// ── Credentials factory ───────────────────────────────────────────────────────

SnmpV3Credentials makeAuthPrivCreds()
{
    SnmpV3Credentials c;
    c.username       = TRAP_USER;
    c.securityLevel  = SnmpSecurityLevel::AuthPriv;
    c.authProtocol   = SnmpAuthProtocol::SHA1;
    c.authPassphrase = TRAP_AUTH_PASS;
    c.privProtocol   = SnmpPrivProtocol::AES128;
    c.privPassphrase = TRAP_PRIV_PASS;
    return c;
}

// ── Trap-sending helpers (via snmptrap CLI in background thread) ──────────────

// Sends an SNMPv2c TRAP2.
void sendV2cTrap(uint16_t port,
                 const std::string& trapOid    = TEST_TRAP_OID,
                 const std::string& varbindOid = TEST_VARBIND_OID,
                 int                value      = 42)
{
    const std::string cmd =
        "snmptrap -v2c -c public udp:127.0.0.1:" + std::to_string(port) +
        " '' " + trapOid +
        " " + varbindOid + " i " + std::to_string(value) +
        " > /dev/null 2>&1";
    system(cmd.c_str());
}

// Sends an SNMPv3 authPriv TRAP2 (SHA1/AES128 with TRAP_USER credentials).
void sendV3AuthPrivTrap(uint16_t port,
                        const std::string& trapOid    = TEST_TRAP_OID,
                        const std::string& varbindOid = TEST_VARBIND_OID,
                        int                value      = 99)
{
    const std::string cmd =
        "snmptrap -v3 -l authPriv"
        " -u " + std::string(TRAP_USER) +
        " -a SHA -A " + std::string(TRAP_AUTH_PASS) +
        " -x AES -X " + std::string(TRAP_PRIV_PASS) +
        " -e " + std::string(SENDER_ENGINE_ID) +
        " udp:127.0.0.1:" + std::to_string(port) +
        " '' " + trapOid +
        " " + varbindOid + " i " + std::to_string(value) +
        " > /dev/null 2>&1";
    system(cmd.c_str());
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
// I. LIFECYCLE TESTS
// ══════════════════════════════════════════════════════════════════════════════

// A freshly constructed receiver is not yet listening.
TEST(SnmpTrapReceiverTests, isListeningFalseBeforeStart)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    EXPECT_FALSE(rx->isListening());
    delete rx;
    delete app;
}

// start() on a high (unprivileged) port succeeds and sets isListening() = true.
TEST(SnmpTrapReceiverTests, startOnHighPortSucceeds)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    EXPECT_TRUE(rx->start());
    EXPECT_TRUE(rx->isListening());
    rx->stop();
    delete rx;
    delete app;
}

// stop() after start() sets isListening() = false.
TEST(SnmpTrapReceiverTests, isListeningFalseAfterStop)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    ASSERT_TRUE(rx->start());
    rx->stop();
    EXPECT_FALSE(rx->isListening());
    delete rx;
    delete app;
}

// Calling start() when already listening is a no-op (returns true, stays alive).
TEST(SnmpTrapReceiverTests, doubleStartIsIdempotent)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    EXPECT_TRUE(rx->start());
    EXPECT_TRUE(rx->start()); // second call must not crash or rebind
    EXPECT_TRUE(rx->isListening());
    rx->stop();
    delete rx;
    delete app;
}

// Calling stop() twice (or before start()) must not crash.
TEST(SnmpTrapReceiverTests, doubleStopIsSafe)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    rx->start();
    rx->stop();
    rx->stop(); // second stop on same object
    EXPECT_FALSE(rx->isListening());
    delete rx;
    delete app;
}

// stop() on an object that was never started must not crash.
TEST(SnmpTrapReceiverTests, stopWithoutStartIsSafe)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    rx->stop();
    EXPECT_FALSE(rx->isListening());
    delete rx;
    delete app;
}

// stop() followed by start() must succeed: the socket is released on stop()
// so the same port can be rebound.
TEST(SnmpTrapReceiverTests, canRestartAfterStop)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    ASSERT_TRUE(rx->start());
    rx->stop();
    EXPECT_TRUE(rx->start());
    EXPECT_TRUE(rx->isListening());
    rx->stop();
    delete rx;
    delete app;
}

// Binding a port that is already held by another receiver must fail and emit
// errorOccurred.
TEST(SnmpTrapReceiverTests, startFailsOnPortInUse)
{
    Application* app = new Application(0, nullptr);

    auto* first = new SnmpTrapReceiver();
    first->setListenPort(TEST_TRAP_PORT);
    ASSERT_TRUE(first->start());

    auto* second = new SnmpTrapReceiver();
    second->setListenPort(TEST_TRAP_PORT);

    std::string errorFromSignal;
    second->errorOccurred.connect([&](const std::string& e) { errorFromSignal = e; });

    const bool secondStarted = second->start();
    EXPECT_FALSE(secondStarted);
    if (!secondStarted)
        EXPECT_FALSE(errorFromSignal.empty()) << "errorOccurred was not emitted on bind failure";

    first->stop();
    delete first;
    delete second;
    delete app;
}

// The destructor must call stop(): after deletion the port must be free for
// a new receiver to bind.
TEST(SnmpTrapReceiverTests, destructorReleasesPort)
{
    Application* app = new Application(0, nullptr);

    {
        auto* rx = new SnmpTrapReceiver();
        rx->setListenPort(TEST_TRAP_PORT);
        ASSERT_TRUE(rx->start());
        delete rx; // destructor calls stop()
    }

    // The same port must now be available.
    auto* rx2 = new SnmpTrapReceiver();
    rx2->setListenPort(TEST_TRAP_PORT);
    EXPECT_TRUE(rx2->start());
    rx2->stop();
    delete rx2;

    delete app;
}

// Adding USM users before start() must not crash; the receiver must start
// and listen normally.
TEST(SnmpTrapReceiverTests, addV3UserBeforeStartDoesNotCrash)
{
    Application* app = new Application(0, nullptr);
    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    rx->addV3User(makeAuthPrivCreds());

    EXPECT_TRUE(rx->start());
    EXPECT_TRUE(rx->isListening());
    rx->stop();
    delete rx;
    delete app;
}

// ══════════════════════════════════════════════════════════════════════════════
// II. TRAP DELIVERY TESTS
// These tests send real UDP packets and require:
//   • SNMP_INTEGRATION_TESTS=1
//   • snmptrap command from the net-snmp-utils package
// ══════════════════════════════════════════════════════════════════════════════

// An SNMPv2c TRAP2 is delivered as trapReceived with the correct trapOid and
// at least one user varbind.
TEST(SnmpTrapReceiverTests, liveAgent_receivesV2cTrap)
{
    if (!integrationTestsEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run trap delivery tests";
    if (!snmptrapAvailable())
        GTEST_SKIP() << "snmptrap not found in PATH (install net-snmp-utils)";

    Application* app = new Application(0, nullptr);

    std::atomic<bool> received{false};
    SnmpTrapInfo      got;

    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    rx->trapReceived.connect([&](const SnmpTrapInfo& trap) {
        got      = trap;
        received = true;
    });
    ASSERT_TRUE(rx->start()) << "Could not bind port " << TEST_TRAP_PORT;

    std::thread sender([]() {
        std::this_thread::sleep_for(30ms);
        sendV2cTrap(TEST_TRAP_PORT);
    });

    const bool ok = pumpUntil(*app, [&] { return received.load(); }, 5s);
    sender.join();

    EXPECT_TRUE(ok) << "trapReceived was not emitted within 5 s";
    EXPECT_EQ(got.trapOid, SnmpOid(TEST_TRAP_OID));
    EXPECT_FALSE(got.varbinds.empty());

    rx->stop();
    delete rx;
    delete app;
}

// An SNMPv3 authPriv TRAP2 is authenticated and delivered with the correct
// security metadata.  The receiver is configured with the specific sender
// engine ID so that key localization (Ku → Kul) can be pre-computed.
TEST(SnmpTrapReceiverTests, liveAgent_receivesV3AuthPrivTrap)
{
    if (!integrationTestsEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run trap delivery tests";
    if (!snmptrapAvailable())
        GTEST_SKIP() << "snmptrap not found in PATH (install net-snmp-utils)";

    Application* app = new Application(0, nullptr);

    std::atomic<bool> received{false};
    SnmpTrapInfo      got;

    // Parse the engine ID that snmptrap will place in the outgoing PDU.
    const auto senderEngineId = parseEngineId(SENDER_ENGINE_ID);

    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    // Register with the sender's specific engine ID so the auth key is
    // pre-localized to that engine: Kul = generate_kul(Ku, senderEngineId).
    rx->addV3User(makeAuthPrivCreds(), senderEngineId);
    rx->trapReceived.connect([&](const SnmpTrapInfo& trap) {
        got      = trap;
        received = true;
    });
    ASSERT_TRUE(rx->start()) << "Could not bind port " << TEST_TRAP_PORT;

    std::thread sender([]() {
        std::this_thread::sleep_for(30ms);
        sendV3AuthPrivTrap(TEST_TRAP_PORT);
    });

    const bool ok = pumpUntil(*app, [&] { return received.load(); }, 5s);
    sender.join();

    EXPECT_TRUE(ok) << "trapReceived was not emitted within 5 s";
    EXPECT_EQ(got.trapOid,        SnmpOid(TEST_TRAP_OID));
    EXPECT_EQ(got.securityName,   std::string(TRAP_USER));
    EXPECT_EQ(got.securityLevel,  SnmpSecurityLevel::AuthPriv);
    EXPECT_FALSE(got.varbinds.empty());

    rx->stop();
    delete rx;
    delete app;
}

// A trap sent with the WRONG passphrase must not be delivered — net-snmp's USM
// layer rejects the packet before it reaches our callback.
TEST(SnmpTrapReceiverTests, liveAgent_wrongPasswordTrapIsDropped)
{
    if (!integrationTestsEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run trap delivery tests";
    if (!snmptrapAvailable())
        GTEST_SKIP() << "snmptrap not found in PATH (install net-snmp-utils)";

    Application* app = new Application(0, nullptr);

    std::atomic<bool> received{false};

    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    rx->addV3User(makeAuthPrivCreds()); // registered with correct password
    rx->trapReceived.connect([&](const SnmpTrapInfo&) { received = true; });
    ASSERT_TRUE(rx->start());

    std::thread sender([]() {
        std::this_thread::sleep_for(30ms);
        // Deliberately wrong passphrases.
        const std::string cmd =
            "snmptrap -v3 -l authPriv"
            " -u testuser -a SHA -A WrongPassword123 -x AES -X WrongPassword456"
            " -e " + std::string(SENDER_ENGINE_ID) +
            " udp:127.0.0.1:" + std::to_string(TEST_TRAP_PORT) +
            " '' " + std::string(TEST_TRAP_OID) +
            " " + std::string(TEST_VARBIND_OID) + " i 1"
            " > /dev/null 2>&1";
        system(cmd.c_str());
    });

    // Pump for 2 s and expect nothing to arrive.
    const bool ok = pumpUntil(*app, [&] { return received.load(); }, 2s);
    sender.join();

    EXPECT_FALSE(ok) << "trapReceived was emitted for a trap with wrong password";

    rx->stop();
    delete rx;
    delete app;
}

// Sending several traps in succession must deliver all of them.
TEST(SnmpTrapReceiverTests, liveAgent_multipleTrapsAllDelivered)
{
    if (!integrationTestsEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run trap delivery tests";
    if (!snmptrapAvailable())
        GTEST_SKIP() << "snmptrap not found in PATH (install net-snmp-utils)";

    Application* app = new Application(0, nullptr);

    constexpr int       TRAP_COUNT = 3;
    std::atomic<int>    count{0};
    std::vector<SnmpTrapInfo> received;

    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    rx->trapReceived.connect([&](const SnmpTrapInfo& trap) {
        received.push_back(trap);
        ++count;
    });
    ASSERT_TRUE(rx->start());

    std::thread sender([]() {
        std::this_thread::sleep_for(30ms);
        for (int i = 0; i < TRAP_COUNT; ++i) {
            sendV2cTrap(TEST_TRAP_PORT, TEST_TRAP_OID, TEST_VARBIND_OID, i + 1);
            std::this_thread::sleep_for(5ms);
        }
    });

    const bool ok = pumpUntil(*app, [&] { return count.load() >= TRAP_COUNT; }, 10s);
    sender.join();

    EXPECT_TRUE(ok) << "Not all traps were delivered (got " << count.load()
                    << " of " << TRAP_COUNT << ") within 10 s";
    EXPECT_EQ(count.load(), TRAP_COUNT);

    // Every delivered trap must carry the correct OID.
    for (const auto& t : received)
        EXPECT_EQ(t.trapOid, SnmpOid(TEST_TRAP_OID));

    rx->stop();
    delete rx;
    delete app;
}

// ══════════════════════════════════════════════════════════════════════════════
// III. CONCURRENT GET + TRAP TEST
//
// Demonstrates (and verifies) the key property of SnmpTrapReceiver: a TRAP
// arriving while a GET is in-flight is handled on the same event-loop
// iteration — no locking required, no events are lost.
//
// Requires:
//   • SNMP_INTEGRATION_TESTS=1
//   • snmptrap in PATH
//   • Local snmpd/pass-persist agent on 127.0.0.1:161 (same as session tests)
// ══════════════════════════════════════════════════════════════════════════════

TEST(SnmpTrapReceiverTests, liveAgent_trapDeliveredConcurrentlyWithGet)
{
    if (!integrationTestsEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run trap delivery tests";
    if (!snmptrapAvailable())
        GTEST_SKIP() << "snmptrap not found in PATH (install net-snmp-utils)";

    Application* app = new Application(0, nullptr);

    // ── Trap receiver ─────────────────────────────────────────────────────────
    std::atomic<bool> trapArrived{false};

    auto* rx = new SnmpTrapReceiver();
    rx->setListenPort(TEST_TRAP_PORT);
    rx->trapReceived.connect([&](const SnmpTrapInfo&) { trapArrived = true; });
    ASSERT_TRUE(rx->start()) << "Could not bind port " << TEST_TRAP_PORT;

    // ── GET session ───────────────────────────────────────────────────────────
    std::atomic<bool> getCompleted{false};

    SnmpV3Config cfg;
    cfg.host    = AGENT_HOST;
    cfg.port    = AGENT_PORT;
    cfg.timeout = 5s;
    cfg.retries = 1;

    auto* session = new SnmpV3Session();
    session->setConfig(cfg);
    session->setCredentials(makeAuthPrivCreds());
    session->getResult.connect([&](const std::vector<SnmpVarBind>&) {
        getCompleted = true;
    });
    session->errorOccurred.connect([&](const std::string&) {
        getCompleted = true; // failure still counts as "completed"
    });

    // Issue the GET — the background thread starts immediately.
    session->get(SnmpOid(READABLE_OID));

    // Send a trap from a separate thread while the GET is in-flight.
    // A short delay lets the GET request go out first.
    std::thread sender([]() {
        std::this_thread::sleep_for(50ms);
        sendV2cTrap(TEST_TRAP_PORT);
    });

    // Both the GET result and the TRAP must arrive on the same event loop
    // without either blocking the other.
    const bool allDone = pumpUntil(
        *app,
        [&] { return getCompleted.load() && trapArrived.load(); },
        10s);
    sender.join();

    EXPECT_TRUE(allDone)
        << "GET completed=" << getCompleted.load()
        << "  trap arrived=" << trapArrived.load()
        << "  (expected both within 10 s)";
    EXPECT_TRUE(getCompleted.load());
    EXPECT_TRUE(trapArrived.load());

    rx->stop();
    delete session;
    delete rx;
    delete app;
}
