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
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
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

// ── Helpers ───────────────────────────────────────────────────────────────────

// ProfilerNode lives in the ThreadPool, so broadcastMessage is emitted from a
// worker thread and keeps firing until Application is destroyed in TearDown().
// Slots therefore share their state through a shared_ptr (never by reference
// to test locals) and are disconnected when the test body ends.
struct BroadcastProbe {
    std::atomic<bool> done{false};
    std::atomic<int>  count{0};
    std::mutex        mutex;
    std::string       firstMessage;  // guarded by mutex
};

class ScopedConnection
{
public:
    explicit ScopedConnection(Connection connection) : m_connection(std::move(connection)) {}
    ~ScopedConnection() { m_connection.disconnect(); }
    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

private:
    Connection m_connection;
};

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

    auto probe = std::make_shared<BroadcastProbe>();
    ScopedConnection connection(pn->broadcastMessage.connect([probe](const std::string& /*msg*/) {
        probe->count.fetch_add(1);
        probe->done.store(true);
    }));

    // Push more than PROFILER_CHUNK_SIZE events to trigger a flush.
    pushEvents(PROFILER_CHUNK_SIZE + 10);

    runUntil(probe->done, 400ms);

    EXPECT_TRUE(probe->done.load())
        << "broadcastMessage was never emitted within 400 ms";
    EXPECT_GT(probe->count.load(), 0);
}

TEST_F(ProfilerNodeFixture, BroadcastMessageContainsValidJson)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    auto probe = std::make_shared<BroadcastProbe>();
    ScopedConnection connection(pn->broadcastMessage.connect([probe](const std::string& msg) {
        std::lock_guard<std::mutex> lock(probe->mutex);
        if (!probe->done.load()) {
            // Publish the message before the flag the main thread waits on.
            probe->firstMessage = msg;
            probe->done.store(true);
        }
    }));

    pushEvents(PROFILER_CHUNK_SIZE + 5);

    runUntil(probe->done, 400ms);

    ASSERT_TRUE(probe->done.load()) << "no broadcast within 400 ms";
    std::string firstMsg;
    {
        std::lock_guard<std::mutex> lock(probe->mutex);
        firstMsg = probe->firstMessage;
    }
    EXPECT_FALSE(firstMsg.empty());
    EXPECT_EQ(firstMsg.front(), '{');
    EXPECT_NE(firstMsg.find("\"type\""), std::string::npos);
}

TEST_F(ProfilerNodeFixture, TraceFileIsCreatedOnChunkFlush)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    auto probe = std::make_shared<BroadcastProbe>();
    ScopedConnection connection(pn->broadcastMessage.connect([probe](const std::string&) {
        probe->done.store(true);
    }));

    pushEvents(PROFILER_CHUNK_SIZE + 1);
    runUntil(probe->done, 400ms);

    EXPECT_TRUE(probe->done.load());

    auto hasTraceFileInDir = [](const std::string& dirPath) {
        if (DIR* d = opendir(dirPath.c_str())) {
            struct dirent* entry;
            while ((entry = readdir(d)) != nullptr) {
                std::string fname(entry->d_name);
                if (fname.rfind("snf_profile_", 0) == 0 &&
                    fname.size() > 5 &&
                    fname.substr(fname.size() - 5) == ".json") {
                    closedir(d);
                    return true;
                }
            }
            closedir(d);
        }
        return false;
    };

    bool fileFound = hasTraceFileInDir(".");
    if (!fileFound) {
        char exePath[1024] = {};
        ssize_t len = ::readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len > 0) {
            std::string exeDir(exePath, len);
            auto sep = exeDir.rfind('/');
            if (sep != std::string::npos) {
                exeDir.resize(sep);
                fileFound = hasTraceFileInDir(exeDir + "/profiler");
            }
        }
    }
    EXPECT_TRUE(fileFound)
        << "expected a snf_profile_*.json file to exist in '.' or '<exe_dir>/profiler'";
}

TEST_F(ProfilerNodeFixture, MacroEventsReachBroadcast)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    auto probe = std::make_shared<BroadcastProbe>();
    ScopedConnection connection(pn->broadcastMessage.connect([probe](const std::string& msg) {
        if (msg.find("\"type\":\"span\"") != std::string::npos) {
            probe->count.fetch_add(1);
            probe->done.store(true);
        }
    }));

    for (int i = 0; i < static_cast<int>(PROFILER_CHUNK_SIZE) + 5; ++i) {
        TRACE_EVENT_BEGIN("macro_cat", "macro_fn");
        TRACE_EVENT_END("macro_cat");
    }

    runUntil(probe->done, 500ms);

    EXPECT_GT(probe->count.load(), 0)
        << "no 'span' JSON messages broadcast within 500 ms";
}

TEST_F(ProfilerNodeFixture, MemSnapshotBroadcastAfterFlush)
{
    auto* pn = snf::profiler::detail::profilerNode();
    ASSERT_NE(pn, nullptr);

    auto probe = std::make_shared<BroadcastProbe>();
    ScopedConnection connection(pn->broadcastMessage.connect([probe](const std::string& msg) {
        if (msg.find("\"type\":\"mem\"") != std::string::npos)
            probe->done.store(true);
    }));

    pushEvents(PROFILER_CHUNK_SIZE + 1);
    runUntil(probe->done, 500ms);

    EXPECT_TRUE(probe->done.load())
        << "no 'mem' JSON snapshot broadcast within 500 ms";
}

} // namespace
