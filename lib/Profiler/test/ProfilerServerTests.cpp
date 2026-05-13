// ProfilerServerTests.cpp — tests for the WebSocket broadcast server.
// Creates a fresh ProfilerServer instance on an OS-assigned port alongside
// the auto-initialised global one (port 8765) to avoid port conflicts.

#include <gtest/gtest.h>

#include "Profiler/ProfilerNode.h"
#include "Profiler/ProfilerServer.h"
#include "Profiler/SysMonitor.h"
#include "Profiler/TraceBuffer.h"
#include "Profiler/TraceEvent.h"
#include "Profiler/integration.h"
#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Timer.h"
#include "SNFCore/Connection.h"
#include "SNFWebSocket/WebSocket.h"
#include "SNFWebSocket/WebSocketServer.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace snf;
using namespace snf::profiler;
using namespace std::chrono_literals;

namespace {

// ── Fixture ───────────────────────────────────────────────────────────────────

class ProfilerServerFixture : public ::testing::Test
{
public:
    void SetUp() override { m_app = new Application(0, nullptr); }

    void TearDown() override
    {
        for (auto* ws : m_clients) {
            if (ws) {
                ws->close();
                delete ws;
            }
        }
        m_clients.clear();
        delete m_app;
        m_app = nullptr;
    }

    void runUntil(std::atomic<bool>& done, std::chrono::milliseconds timeout)
    {
        EventLoop* mainLoop = m_app->getOrCreateCurrentThreadEventLoop();

        Timer guard;
        guard.setSingleShot(true);
        guard.timeout.connect([mainLoop]() {
            mainLoop->post([mainLoop]() { mainLoop->stop(); });
        });
        guard.start(timeout);

        std::thread watcher([&]() {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            while (!done.load() &&
                   std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(5ms);
            mainLoop->post([mainLoop]() { mainLoop->stop(); });
        });

        m_app->run();
        watcher.join();
    }

    Application*          m_app    = nullptr;
    std::vector<WebSocket*> m_clients;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(ProfilerServerFixture, GlobalProfilerServerExists)
{
    EXPECT_NE(snf::profiler::detail::profilerServer(), nullptr);
}

TEST_F(ProfilerServerFixture, ClientReceivesHelloOnConnect)
{
    // Connect a raw WebSocket client to the globally auto-started server.
    auto* server = snf::profiler::detail::profilerServer();
    ASSERT_NE(server, nullptr);

    uint16_t port = server->port();
    ASSERT_GT(port, 0u) << "server must be listening on a valid port";

    auto received = std::make_shared<std::string>();
    auto gotMsg   = std::make_shared<std::atomic<bool>>(false);

    auto* client = new WebSocket();
    m_clients.push_back(client);

    auto c1 = client->textMessageReceived.connect([received, gotMsg](const std::string& msg) {
        if (!gotMsg->exchange(true))
            *received = msg;
    });

    auto c2 = client->errorOccurred.connect([gotMsg](const std::string& err) {
        ADD_FAILURE() << "WebSocket client error: " << err;
        gotMsg->store(true);
    });

    client->connectToHost(HostAddress::LocalHost, port);
    runUntil(*gotMsg, 2s);
    c1.disconnect(); c2.disconnect();

    ASSERT_TRUE(gotMsg->load()) << "no message received from ProfilerServer within 2s";
    EXPECT_NE(received->find("\"type\":\"hello\""), std::string::npos)
        << "first message must contain \"type\":\"hello\", got: " << *received;
    EXPECT_NE(received->find("\"version\""), std::string::npos);
    EXPECT_NE(received->find("\"capabilities\""), std::string::npos);
}

TEST_F(ProfilerServerFixture, HelloHandshakeContainsExpectedCapabilities)
{
    auto* server = snf::profiler::detail::profilerServer();
    ASSERT_NE(server, nullptr);

    uint16_t port = server->port();
    auto hello    = std::make_shared<std::string>();
    auto gotHello = std::make_shared<std::atomic<bool>>(false);

    auto* client = new WebSocket();
    m_clients.push_back(client);

    auto conn = client->textMessageReceived.connect([hello, gotHello](const std::string& msg) {
        if (!gotHello->exchange(true)) *hello = msg;
    });
    client->connectToHost(HostAddress::LocalHost, port);
    runUntil(*gotHello, 2s);
    conn.disconnect();

    ASSERT_TRUE(gotHello->load());
    EXPECT_NE(hello->find("trace"),  std::string::npos);
    EXPECT_NE(hello->find("memory"), std::string::npos);
    EXPECT_NE(hello->find("sys"),    std::string::npos);
}

TEST_F(ProfilerServerFixture, MultipleClientsAllReceiveHello)
{
    auto* server = snf::profiler::detail::profilerServer();
    ASSERT_NE(server, nullptr);
    uint16_t port = server->port();

    constexpr int kClients = 3;
    auto helloCount = std::make_shared<std::atomic<int>>(0);
    auto allDone    = std::make_shared<std::atomic<bool>>(false);

    std::vector<Connection> conns;
    for (int i = 0; i < kClients; ++i) {
        auto* c = new WebSocket();
        m_clients.push_back(c);
        conns.push_back(c->textMessageReceived.connect([helloCount, allDone](const std::string& msg) {
            if (msg.find("\"type\":\"hello\"") != std::string::npos) {
                if (helloCount->fetch_add(1) + 1 >= kClients)
                    allDone->store(true);
            }
        }));
        c->connectToHost(HostAddress::LocalHost, port);
    }

    runUntil(*allDone, 3s);
    for (auto& c : conns) c.disconnect();

    EXPECT_EQ(helloCount->load(), kClients)
        << "not all clients received hello within 3s";
}

TEST_F(ProfilerServerFixture, ClientReceivesSpanAfterChunkFlush)
{
    auto* server = snf::profiler::detail::profilerServer();
    ASSERT_NE(server, nullptr);
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    uint16_t port = server->port();

    std::atomic<bool> connected{false};
    auto gotHello = std::make_shared<std::atomic<bool>>(false);
    auto gotSpan  = std::make_shared<std::atomic<bool>>(false);

    auto* client = new WebSocket();
    m_clients.push_back(client);

    auto c1 = client->connected.connect([&]() { connected.store(true); });
    auto c2 = client->textMessageReceived.connect([gotHello, gotSpan](const std::string& msg) {
        if (msg.find("\"type\":\"hello\"") != std::string::npos)
            gotHello->store(true);
        if (msg.find("\"type\":\"span\"") != std::string::npos)
            gotSpan->store(true);
    });
    client->connectToHost(HostAddress::LocalHost, port);

    // Wait for the hello message — this guarantees the server-side per-connection
    // WebSocket is in m_clients (hello is sent inside onNewConnection()).
    runUntil(connected, 2s);
    c1.disconnect();
    ASSERT_TRUE(connected.load()) << "client never connected";
    runUntil(*gotHello, 2s);
    ASSERT_TRUE(gotHello->load()) << "client never received hello";

    // Push a full chunk to trigger broadcast.
    for (int i = 0; i < static_cast<int>(PROFILER_CHUNK_SIZE) + 5; ++i) {
        TraceEvent e{};
        e.timestamp_ns  = 1000000u * static_cast<uint64_t>(i);
        e.thread_id     = 1;
        e.phase         = (i % 2 == 0) ? EventPhase::BEGIN : EventPhase::END;
        e.category      = "srv_test";
        e.name          = "srv_fn";
        e.payload_bytes = 0;
        TraceBuffer::current().push(e);
    }

    runUntil(*gotSpan, 3s);
    c2.disconnect();
    EXPECT_TRUE(gotSpan->load())
        << "client did not receive a 'span' message within 3 s";
}

TEST_F(ProfilerServerFixture, DisconnectedClientDoesNotCrash)
{
    auto* server = snf::profiler::detail::profilerServer();
    ASSERT_NE(server, nullptr);
    uint16_t port = server->port();

    auto connected    = std::make_shared<std::atomic<bool>>(false);
    auto disconnected = std::make_shared<std::atomic<bool>>(false);

    auto* client = new WebSocket();
    m_clients.push_back(client);

    auto c1 = client->connected.connect([connected]() { connected->store(true); });
    auto c2 = client->disconnected.connect([disconnected]() { disconnected->store(true); });

    client->connectToHost(HostAddress::LocalHost, port);
    runUntil(*connected, 2s);
    c1.disconnect();
    ASSERT_TRUE(connected->load());

    client->close();
    runUntil(*disconnected, 5s);
    c2.disconnect();
    EXPECT_TRUE(disconnected->load());

    // After disconnect, push a chunk — must not crash.
    for (int i = 0; i < static_cast<int>(PROFILER_CHUNK_SIZE) + 1; ++i) {
        TraceEvent e{};
        e.phase    = EventPhase::INSTANT;
        e.category = "post_dc";
        e.name     = "fn";
        TraceBuffer::current().push(e);
    }
    // Let the drain fire once.
    std::this_thread::sleep_for(50ms);
    m_app->loopOnce();
    // If we reach here without crashing the test passes.
    SUCCEED();
}

} // namespace
