#include <gtest/gtest.h>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFSnmp/SnmpTypes.h>
#include <SNFSnmp/SnmpV3Session.h>
#include <SNFSnmp/SnmpVarBind.h>
#include <SNFSnmp/SnmpValue.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace snf;
using namespace std::chrono_literals;

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

bool pumpPendingWorkUntil(Application& app, const std::function<bool()>& predicate,
                           std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (EventLoop* loop = app.getOrCreateCurrentThreadEventLoop())
            loop->runPendingWork();
        if (predicate())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    if (EventLoop* loop = app.getOrCreateCurrentThreadEventLoop())
        loop->runPendingWork();
    return predicate();
}

} // namespace

// ── Configuration API tests (no network) ─────────────────────────────────────

TEST(SnmpV3SessionTests, configRoundTrip)
{
    Application* app = new Application(0, nullptr);

    auto* session = new SnmpV3Session();

    SnmpV3Config cfg;
    cfg.host    = "10.0.0.1";
    cfg.port    = 1161;
    cfg.retries = 5;
    cfg.timeout = std::chrono::seconds(10);
    session->setConfig(cfg);

    const auto read = session->config();
    EXPECT_EQ(read.host,    "10.0.0.1");
    EXPECT_EQ(read.port,    1161);
    EXPECT_EQ(read.retries, 5);
    EXPECT_EQ(read.timeout, std::chrono::seconds(10));

    delete session;
    delete app;
}

TEST(SnmpV3SessionTests, credentialsRoundTrip)
{
    Application* app = new Application(0, nullptr);

    auto* session = new SnmpV3Session();

    SnmpV3Credentials creds;
    creds.username       = "testUser";
    creds.securityLevel  = SnmpSecurityLevel::AuthPriv;
    creds.authProtocol   = SnmpAuthProtocol::SHA1;
    creds.authPassphrase = "authPass123";
    creds.privProtocol   = SnmpPrivProtocol::AES128;
    creds.privPassphrase = "privPass123";
    session->setCredentials(creds);

    const auto read = session->credentials();
    EXPECT_EQ(read.username,       "testUser");
    EXPECT_EQ(read.securityLevel,  SnmpSecurityLevel::AuthPriv);
    EXPECT_EQ(read.authProtocol,   SnmpAuthProtocol::SHA1);
    EXPECT_EQ(read.authPassphrase, "authPass123");
    EXPECT_EQ(read.privProtocol,   SnmpPrivProtocol::AES128);
    EXPECT_EQ(read.privPassphrase, "privPass123");

    delete session;
    delete app;
}

TEST(SnmpV3SessionTests, defaultCredentialsAreNoAuthNoPriv)
{
    Application* app = new Application(0, nullptr);
    auto* session = new SnmpV3Session();

    const auto creds = session->credentials();
    EXPECT_EQ(creds.securityLevel, SnmpSecurityLevel::NoAuthNoPriv);
    EXPECT_EQ(creds.authProtocol,  SnmpAuthProtocol::None);
    EXPECT_EQ(creds.privProtocol,  SnmpPrivProtocol::None);

    delete session;
    delete app;
}

TEST(SnmpV3SessionTests, defaultPortIs161)
{
    Application* app = new Application(0, nullptr);
    auto* session = new SnmpV3Session();
    EXPECT_EQ(session->config().port, 161);
    delete session;
    delete app;
}

// ── Network error handling (unreachable host) ──────────────────────────────────
//
// Sends a GET to an address that is guaranteed to be unreachable or produce no
// response (localhost:1 is almost always closed). The test verifies that the
// request completes with an errorOccurred signal rather than hanging.

TEST(SnmpV3SessionTests, unreachableHostEmitsErrorWithinTimeout)
{
    Application* app = new Application(0, nullptr);

    auto* session = new SnmpV3Session();

    SnmpV3Config cfg;
    cfg.host    = "127.0.0.1";
    cfg.port    = 1;         // almost certainly nothing listening here
    cfg.timeout = std::chrono::milliseconds(500);
    cfg.retries = 0;
    session->setConfig(cfg);

    SnmpV3Credentials creds;
    creds.username      = "public";
    creds.securityLevel = SnmpSecurityLevel::NoAuthNoPriv;
    session->setCredentials(creds);

    std::atomic<bool> gotError{false};
    session->errorOccurred.connect([&](const std::string&) {
        gotError = true;
    });

    session->get(SnmpOid("1.3.6.1.2.1.1.1.0"));

    // Wait up to 5 s for the error; net-snmp may take a moment to time out.
    const bool completed = pumpPendingWorkUntil(*app, [&] { return gotError.load(); }, 5s);
    EXPECT_TRUE(completed) << "errorOccurred was not emitted within 5 seconds";

    delete session;
    delete app;
}

// ── Live-agent integration tests ───────────────────────────────────────────────
//
// These tests require snmp_agent_sim.py running under a local net-snmp daemon
// at 127.0.0.1:161 with the following USM credentials:
//
//   username : testuser
//   auth     : SHA1  / "authpass"
//   priv     : AES128 / "privpass"
//
// Guard them with an environment variable so they are skipped in CI:
//
//   SNMP_INTEGRATION_TESTS=1 ctest --preset native-debug -R SNFSnmpTests
//
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr const char* LIVE_HOST          = "127.0.0.1";
constexpr uint16_t    LIVE_PORT          = 161;
constexpr const char* LIVE_USER          = "testuser";
constexpr const char* LIVE_AUTH_PASS     = "authpass";
constexpr const char* LIVE_PRIV_PASS     = "privpass";

// Integer OID in the default snmp_agent_sim.py database; default value 20.
constexpr const char* WRITABLE_INT_OID    = "1.3.6.1.4.1.99999.1.2.4.0";

// OctetString OID in the default snmp_agent_sim.py database; default value "21407".
constexpr const char* WRITABLE_STR_OID    = "1.3.6.1.4.1.99999.1.2.2.0";

// Root of a subtree that contains 6 OIDs in the default simulator database.
constexpr const char* WALK_SUBTREE_ROOT   = "1.3.6.1.4.1.99999.1.2";

bool liveAgentEnabled()
{
    const char* v = std::getenv("SNMP_INTEGRATION_TESTS");
    return v && std::string(v) == "1";
}

SnmpV3Session* makeLiveSession()
{
    SnmpV3Config cfg;
    cfg.host    = LIVE_HOST;
    cfg.port    = LIVE_PORT;
    cfg.timeout = std::chrono::seconds(5);
    cfg.retries = 1;

    SnmpV3Credentials creds;
    creds.username       = LIVE_USER;
    creds.securityLevel  = SnmpSecurityLevel::AuthPriv;
    creds.authProtocol   = SnmpAuthProtocol::SHA1;
    creds.authPassphrase = LIVE_AUTH_PASS;
    creds.privProtocol   = SnmpPrivProtocol::AES128;
    creds.privPassphrase = LIVE_PRIV_PASS;

    auto* session = new SnmpV3Session();
    session->setConfig(cfg);
    session->setCredentials(creds);
    return session;
}

} // namespace

// GET returns a value with the expected OID and Integer32 type.
TEST(SnmpV3SessionTests, liveAgent_getReturnsIntegerValue)
{
    if (!liveAgentEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run live-agent tests";

    Application* app = new Application(0, nullptr);
    auto* session = makeLiveSession();

    std::atomic<bool> done{false};
    std::string errorMsg;
    SnmpVarBind received;

    session->errorOccurred.connect([&](const std::string& e) {
        errorMsg = e;
        done     = true;
    });
    session->getResult.connect([&](const std::vector<SnmpVarBind>& vbs) {
        if (!vbs.empty())
            received = vbs[0];
        done = true;
    });

    session->get(SnmpOid(WRITABLE_INT_OID));

    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return done.load(); }, 10s))
        << "No response within 10 s";
    EXPECT_TRUE(errorMsg.empty()) << "SNMP error: " << errorMsg;
    EXPECT_EQ(received.oid, SnmpOid(WRITABLE_INT_OID));
    EXPECT_EQ(received.value.type(), SnmpValueType::Integer32);

    delete session;
    delete app;
}

// SET writes a value; subsequent GET reads the same value back.
//
// Note: net-snmp's pass_persist agent briefly returns NoSuchInstance on the
// first GET after any SET while snmpd re-discovers the subtree. A warm-up GET
// (which absorbs that NoSuchInstance) is issued first; the follow-up GET
// reliably returns the updated value.
TEST(SnmpV3SessionTests, liveAgent_setAndGetRoundTrip)
{
    if (!liveAgentEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run live-agent tests";

    Application* app = new Application(0, nullptr);
    auto* session = makeLiveSession();

    constexpr std::int32_t WRITTEN = 99;
    std::atomic<bool>  setDone   {false};
    std::atomic<bool>  warmupDone{false};
    std::atomic<bool>  verifyDone{false};
    std::string        errorMsg;
    std::int32_t       readBack = -1;

    session->errorOccurred.connect([&](const std::string& e) {
        errorMsg   = e;
        setDone    = true;
        warmupDone = true;
        verifyDone = true;
    });
    session->setResult.connect([&]() { setDone = true; });
    session->getResult.connect([&](const std::vector<SnmpVarBind>& vbs) {
        if (!warmupDone.exchange(true)) {
            return; // absorb the first (potentially NoSuchInstance) response
        }
        if (!vbs.empty() && vbs[0].value.type() == SnmpValueType::Integer32)
            readBack = vbs[0].value.toInt32();
        verifyDone = true;
    });

    SnmpVarBind vb;
    vb.oid   = SnmpOid(WRITABLE_INT_OID);
    vb.value = SnmpValue::fromInteger32(WRITTEN);
    session->set(vb);

    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return setDone.load(); }, 10s))
        << "SET did not complete within 10 s";

    session->get(SnmpOid(WRITABLE_INT_OID)); // warm-up: absorbs NoSuchInstance
    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return warmupDone.load(); }, 10s))
        << "Warm-up GET did not complete within 10 s";

    session->get(SnmpOid(WRITABLE_INT_OID)); // verify: returns updated value
    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return verifyDone.load(); }, 10s))
        << "Verify GET did not complete within 10 s";

    EXPECT_TRUE(errorMsg.empty()) << "SNMP error: " << errorMsg;
    EXPECT_EQ(readBack, WRITTEN);

    delete session;
    delete app;
}

// WALK returns all OIDs under the subtree root, and every returned OID is
// within that subtree.
TEST(SnmpV3SessionTests, liveAgent_walkReturnsSubtree)
{
    if (!liveAgentEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run live-agent tests";

    Application* app = new Application(0, nullptr);
    auto* session = makeLiveSession();

    std::atomic<bool> done{false};
    std::string errorMsg;
    std::vector<SnmpVarBind> results;

    session->errorOccurred.connect([&](const std::string& e) {
        errorMsg = e;
        done     = true;
    });
    session->walkResult.connect([&](const std::vector<SnmpVarBind>& vbs) {
        results = vbs;
        done    = true;
    });

    const SnmpOid subtree(WALK_SUBTREE_ROOT);
    session->walk(subtree);

    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return done.load(); }, 10s))
        << "No response within 10 s";
    EXPECT_TRUE(errorMsg.empty()) << "SNMP error: " << errorMsg;
    EXPECT_GE(results.size(), 1u) << "Walk returned no results";
    for (const auto& vb : results)
        EXPECT_TRUE(subtree.isPrefixOf(vb.oid))
            << vb.oid.toString() << " is outside subtree " << subtree.toString();

    delete session;
    delete app;
}

// SET an OctetString value; subsequent GET reads it back as OctetString.
// Uses the same warm-up pattern as the Integer32 test to absorb the one-shot
// NoSuchInstance that net-snmp emits immediately after a pass_persist SET.
TEST(SnmpV3SessionTests, liveAgent_setAndGetOctetStringRoundTrip)
{
    if (!liveAgentEnabled())
        GTEST_SKIP() << "Set SNMP_INTEGRATION_TESTS=1 to run live-agent tests";

    Application* app = new Application(0, nullptr);
    auto* session = makeLiveSession();

    const std::string WRITTEN = "hello-snmp";
    std::atomic<bool>  setDone   {false};
    std::atomic<bool>  warmupDone{false};
    std::atomic<bool>  verifyDone{false};
    std::string        errorMsg;
    std::string        readBack;

    session->errorOccurred.connect([&](const std::string& e) {
        errorMsg   = e;
        setDone    = true;
        warmupDone = true;
        verifyDone = true;
    });
    session->setResult.connect([&]() { setDone = true; });
    session->getResult.connect([&](const std::vector<SnmpVarBind>& vbs) {
        if (!warmupDone.exchange(true)) {
            return; // absorb the first post-SET response
        }
        if (!vbs.empty() && vbs[0].value.type() == SnmpValueType::OctetString)
            readBack = vbs[0].value.toString();
        verifyDone = true;
    });

    SnmpVarBind vb;
    vb.oid   = SnmpOid(WRITABLE_STR_OID);
    vb.value = SnmpValue::fromOctetString(WRITTEN);
    session->set(vb);

    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return setDone.load(); }, 10s))
        << "SET did not complete within 10 s";

    session->get(SnmpOid(WRITABLE_STR_OID)); // warm-up
    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return warmupDone.load(); }, 10s))
        << "Warm-up GET did not complete within 10 s";

    session->get(SnmpOid(WRITABLE_STR_OID)); // verify
    ASSERT_TRUE(pumpPendingWorkUntil(*app, [&] { return verifyDone.load(); }, 10s))
        << "Verify GET did not complete within 10 s";

    EXPECT_TRUE(errorMsg.empty()) << "SNMP error: " << errorMsg;
    EXPECT_EQ(readBack, WRITTEN);

    delete session;
    delete app;
}
