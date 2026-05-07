// ProfilerNodeTests.cpp — integration tests for drain-timer and chunk flush.
// Uses the globally auto-initialised ProfilerNode that Application creates via
// detail::init().  We access it through the getter exposed in integration.h.

#include <gtest/gtest.h>

#include "Profiler/Macros.h"
#include "Profiler/ProfilerNode.h"
#include "Profiler/TraceBuffer.h"
#include "Profiler/TraceEvent.h"
#include "Profiler/integration.h"
#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Timer.h"

#include <atomic>
#include <chrono>
#include <dirent.h>
#include <string>
#include <thread>
#include <vector>

using namespace snf;
using namespace snf::profiler;
using namespace std::chrono_literals;

namespace {

// ── Fixture ───────────────────────────────────────────────────────────────────

class ProfilerNodeFixture : public ::testing::Test
{
public:
    void SetUp() override { m_app = new Application(0, nullptr); }

    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }

    // Run the main event loop until 'done' is true or 'timeout' expires.
    // Stops by posting to the MAIN thread's EventLoop (same thread as the test).
    void runUntil(std::atomic<bool>& done, std::chrono::milliseconds timeout)
    {
        EventLoop* mainLoop = m_app->getOrCreateCurrentThreadEventLoop();

        // Watchdog timer: stops main loop after timeout regardless.
        Timer guard;
        guard.setSingleShot(true);
        guard.timeout.connect([mainLoop]() {
            mainLoop->post([mainLoop]() { mainLoop->stop(); });
        });
        guard.start(timeout);

        // Poll in a background thread; stop main loop once condition is met.
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

    Application* m_app = nullptr;
};

// ── Helper ────────────────────────────────────────────────────────────────────

static void pushEvents(int n,
                       const char* cat  = "test_cat",
                       const char* name = "test_fn")
{
    for (int i = 0; i < n; ++i) {
        TraceEvent e{};
        e.timestamp_ns  = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        e.thread_id     = static_cast<uint32_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        e.phase         = (i % 2 == 0) ? EventPhase::BEGIN : EventPhase::END;
        e.category      = cat;
        e.name          = name;
        e.payload_bytes = 0;
        TraceBuffer::current().push(e);
    }
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(ProfilerNodeFixture, GlobalProfilerNodeExists)
{
    EXPECT_NE(snf::profiler::detail::profilerNode(), nullptr);
}

TEST_F(ProfilerNodeFixture, BroadcastMessageSignalFiresAfterChunkFull)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    std::atomic<bool> received{false};
    std::atomic<int>  count{0};

    pn->broadcastMessage.connect([&](const std::string& /*msg*/) {
        count.fetch_add(1);
        received.store(true);
    });

    // Push more than PROFILER_CHUNK_SIZE events to trigger a flush.
    pushEvents(PROFILER_CHUNK_SIZE + 10);

    runUntil(received, 400ms);

    EXPECT_TRUE(received.load())
        << "broadcastMessage was never emitted within 400 ms";
    EXPECT_GT(count.load(), 0);
}

TEST_F(ProfilerNodeFixture, BroadcastMessageContainsValidJson)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    std::string firstMsg;
    std::atomic<bool> got{false};

    pn->broadcastMessage.connect([&](const std::string& msg) {
        if (!got.exchange(true))
            firstMsg = msg;
    });

    pushEvents(PROFILER_CHUNK_SIZE + 5);

    runUntil(got, 400ms);

    ASSERT_TRUE(got.load()) << "no broadcast within 400 ms";
    EXPECT_FALSE(firstMsg.empty());
    EXPECT_EQ(firstMsg.front(), '{');
    EXPECT_NE(firstMsg.find("\"type\""), std::string::npos);
}

TEST_F(ProfilerNodeFixture, TraceFileIsCreatedOnChunkFlush)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    std::atomic<bool> flushed{false};
    pn->broadcastMessage.connect([&](const std::string&) {
        flushed.store(true);
    });

    pushEvents(PROFILER_CHUNK_SIZE + 1);
    runUntil(flushed, 400ms);

    EXPECT_TRUE(flushed.load());

    bool fileFound = false;
    if (DIR* d = opendir(".")) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string fname(entry->d_name);
            if (fname.rfind("snf_profile_", 0) == 0 &&
                fname.size() > 5 &&
                fname.substr(fname.size() - 5) == ".json") {
                fileFound = true;
                break;
            }
        }
        closedir(d);
    }
    EXPECT_TRUE(fileFound) << "expected a snf_profile_*.json file to exist";
}

TEST_F(ProfilerNodeFixture, MacroEventsReachBroadcast)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    std::atomic<int> spanCount{0};
    std::atomic<bool> gotSpan{false};

    pn->broadcastMessage.connect([&](const std::string& msg) {
        if (msg.find("\"type\":\"span\"") != std::string::npos) {
            spanCount.fetch_add(1);
            gotSpan.store(true);
        }
    });

    for (int i = 0; i < static_cast<int>(PROFILER_CHUNK_SIZE) + 5; ++i) {
        TRACE_EVENT_BEGIN("macro_cat", "macro_fn");
        TRACE_EVENT_END("macro_cat");
    }

    runUntil(gotSpan, 500ms);

    EXPECT_GT(spanCount.load(), 0)
        << "no 'span' JSON messages broadcast within 500 ms";
}

TEST_F(ProfilerNodeFixture, MemSnapshotBroadcastAfterFlush)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    std::atomic<bool> gotMem{false};

    pn->broadcastMessage.connect([&](const std::string& msg) {
        if (msg.find("\"type\":\"mem\"") != std::string::npos)
            gotMem.store(true);
    });

    pushEvents(PROFILER_CHUNK_SIZE + 1);
    runUntil(gotMem, 500ms);

    EXPECT_TRUE(gotMem.load())
        << "no 'mem' JSON snapshot broadcast within 500 ms";
}

} // namespace
