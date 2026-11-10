// SysMonitorTests.cpp — tests for /proc-based system sampler.

#include <gtest/gtest.h>

#include "Profiler/SysMonitor.h"
#include "Profiler/integration.h"
#include "SNFCore/Application.h"
#include "SNFCore/Connection.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Timer.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace snf;
using namespace snf::profiler;
using namespace std::chrono_literals;

namespace {

// ── Fixture ───────────────────────────────────────────────────────────────────

class SysMonitorFixture : public ::testing::Test
{
public:
    void SetUp() override { m_app = new Application(0, nullptr); }

    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }

    void runUntil(std::atomic<bool>& done, std::chrono::milliseconds timeout)
    {
        EventLoop* mainLoop = m_app->getOrCreateCurrentThreadEventLoop();

        // Exactly one stop per call: a second one (e.g. from an extra
        // watchdog timer) would stay queued and end the next run() at once.
        // The watcher stops the loop when done or at the deadline.
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

// ── /proc/net/dev parsing and rates (no Application needed) ───────────────────

// Layout of /proc/net/dev: 8 receive columns, then 8 transmit columns.
// Every column holds a different value so a wrong offset is visible.
static const char* kNetDev =
    "Inter-|   Receive                                                |  Transmit\n"
    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
    "    lo: 1000 11 12 13 14 15 16 17 2000 21 22 23 24 25 26 27\n"
    "  eno1: 3000 31 32 33 34 35 36 37 4000 41 42 43 44 45 46 47\n";

TEST(SysMonitorParse, NetDevReadsReceiveAndTransmitBytes)
{
    std::istringstream in(kNetDev);
    auto stats = snf::profiler::detail::parseNetDev(in);

    ASSERT_EQ(stats.size(), 2u);
    EXPECT_EQ(stats["lo"].rx, 1000u);
    EXPECT_EQ(stats["lo"].tx, 2000u);
    EXPECT_EQ(stats["eno1"].rx, 3000u);
    EXPECT_EQ(stats["eno1"].tx, 4000u);
}

TEST(SysMonitorParse, NetDevAcceptsNoSpaceAfterColon)
{
    // Older kernels print the receive bytes right after the colon.
    std::istringstream in(
        "Inter-|   Receive\n"
        " face |bytes\n"
        "  eth0:5000 1 2 3 4 5 6 7 6000 1 2 3 4 5 6 7\n");
    auto stats = snf::profiler::detail::parseNetDev(in);

    ASSERT_EQ(stats.count("eth0"), 1u);
    EXPECT_EQ(stats["eth0"].rx, 5000u);
    EXPECT_EQ(stats["eth0"].tx, 6000u);
}

TEST(SysMonitorParse, RateUsesElapsedTime)
{
    EXPECT_EQ(snf::profiler::detail::bytesPerSecond(0, 1000, 250ms), 4000u);
    EXPECT_EQ(snf::profiler::detail::bytesPerSecond(500, 1500, 2s), 500u);
}

TEST(SysMonitorParse, RateIsZeroWhenCounterGoesBack)
{
    // Interface reset or counter wrap: no huge value from unsigned underflow.
    EXPECT_EQ(snf::profiler::detail::bytesPerSecond(1000, 10, 1s), 0u);
}

TEST(SysMonitorParse, RateIsZeroWithoutElapsedTime)
{
    EXPECT_EQ(snf::profiler::detail::bytesPerSecond(0, 1000, 0ns), 0u);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(SysMonitorFixture, GlobalSysMonitorExists)
{
    EXPECT_NE(snf::profiler::detail::sysMonitor(), nullptr);
}

TEST_F(SysMonitorFixture, SampleReadyEmittedWithin600ms)
{
    auto* sm = snf::profiler::detail::sysMonitor();
    ASSERT_NE(sm, nullptr);

    // Use shared_ptr so the captured flag remains valid after TestBody returns
    // even if the SysMonitor timer fires again during TearDown.
    auto gotSample = std::make_shared<std::atomic<bool>>(false);
    auto conn = sm->sampleReady.connect([gotSample](const SysSample&) {
        gotSample->store(true);
    });

    // SysMonitor interval=500ms + calibration tick = first real sample at ~1000ms.
    // Use a generous timeout to absorb ASAN overhead and OS scheduling jitter.
    runUntil(*gotSample, 5000ms);
    conn.disconnect();

    EXPECT_TRUE(gotSample->load())
        << "SysMonitor did not emit sampleReady within 5000 ms";
}

TEST_F(SysMonitorFixture, SampleHasNonZeroCpuVector)
{
    auto* sm = snf::profiler::detail::sysMonitor();
    ASSERT_NE(sm, nullptr);

    auto gotSample = std::make_shared<std::atomic<bool>>(false);
    auto captured  = std::make_shared<SysSample>();

    auto conn = sm->sampleReady.connect([gotSample, captured](const SysSample& s) {
        if (!gotSample->exchange(true))
            *captured = s;
    });

    runUntil(*gotSample, 5000ms);
    conn.disconnect();

    ASSERT_TRUE(gotSample->load());
    EXPECT_FALSE(captured->cpu_usage.empty())
        << "cpu_usage vector must not be empty";
}

TEST_F(SysMonitorFixture, CpuUsageValuesInRange)
{
    auto* sm = snf::profiler::detail::sysMonitor();
    ASSERT_NE(sm, nullptr);

    // Wait for TWO samples so we see real CPU-delta data (first sample is calibration).
    auto samples = std::make_shared<std::vector<SysSample>>();
    auto gotTwo  = std::make_shared<std::atomic<bool>>(false);
    auto mtx     = std::make_shared<std::mutex>();

    auto conn = sm->sampleReady.connect([samples, gotTwo, mtx](const SysSample& s) {
        std::lock_guard<std::mutex> lk(*mtx);
        samples->push_back(s);
        if (samples->size() >= 2) gotTwo->store(true);
    });

    runUntil(*gotTwo, 6000ms);
    conn.disconnect();

    std::lock_guard<std::mutex> lk(*mtx);
    ASSERT_GE(samples->size(), 1u);
    const auto& s = samples->back();
    for (float usage : s.cpu_usage) {
        EXPECT_GE(usage, 0.0f);
        EXPECT_LE(usage, 1.0f)
            << "cpu_usage value out of [0,1] range: " << usage;
    }
}

TEST_F(SysMonitorFixture, RamValuesArePositive)
{
    auto* sm = snf::profiler::detail::sysMonitor();
    ASSERT_NE(sm, nullptr);

    auto gotSample = std::make_shared<std::atomic<bool>>(false);
    auto captured  = std::make_shared<SysSample>();

    auto conn = sm->sampleReady.connect([gotSample, captured](const SysSample& s) {
        if (!gotSample->exchange(true))
            *captured = s;
    });

    runUntil(*gotSample, 5000ms);
    conn.disconnect();

    ASSERT_TRUE(gotSample->load());
    EXPECT_GT(captured->ram_used_bytes + captured->ram_free_bytes, 0u)
        << "both ram_used_bytes and ram_free_bytes are zero";
}

TEST_F(SysMonitorFixture, TimestampIsNonZero)
{
    auto* sm = snf::profiler::detail::sysMonitor();
    ASSERT_NE(sm, nullptr);

    auto gotSample = std::make_shared<std::atomic<bool>>(false);
    auto ts        = std::make_shared<std::atomic<uint64_t>>(0);

    auto conn = sm->sampleReady.connect([gotSample, ts](const SysSample& s) {
        if (!gotSample->exchange(true))
            ts->store(s.timestamp_ns);
    });

    runUntil(*gotSample, 5000ms);
    conn.disconnect();

    ASSERT_TRUE(gotSample->load());
    EXPECT_GT(ts->load(), 0u);
}

TEST_F(SysMonitorFixture, SamplesArriveRepeatedly)
{
    auto* sm = snf::profiler::detail::sysMonitor();
    ASSERT_NE(sm, nullptr);

    auto count    = std::make_shared<std::atomic<int>>(0);
    auto gotThree = std::make_shared<std::atomic<bool>>(false);

    auto conn = sm->sampleReady.connect([count, gotThree](const SysSample&) {
        if (count->fetch_add(1) + 1 >= 3)
            gotThree->store(true);
    });

    // SysMonitor fires every 500 ms; calibration on tick 1, real samples on ticks 2+.
    // Three real samples arrive at ~1000 ms, ~1500 ms, ~2000 ms.
    // Use 8000 ms to absorb ASAN overhead and OS scheduling jitter.
    runUntil(*gotThree, 8000ms);
    conn.disconnect();

    EXPECT_GE(count->load(), 3)
        << "expected at least 3 SysSamples within 8 s";
}

TEST_F(SysMonitorFixture, FreshSysMonitorEmitsSamples)
{
    // Create a second SysMonitor (independent of the global auto-init one)
    // to verify the class works standalone.
    auto* extraSm = new SysMonitor(nullptr, 100ms);

    // 100 ms interval: calibration at ~100 ms, first real sample at ~200 ms.
    auto gotSample = std::make_shared<std::atomic<bool>>(false);
    auto conn = extraSm->sampleReady.connect([gotSample](const SysSample&) {
        gotSample->store(true);
    });

    runUntil(*gotSample, 2000ms);
    conn.disconnect();

    EXPECT_TRUE(gotSample->load())
        << "fresh SysMonitor(100ms interval) never emitted a sample within 2000 ms";

    delete extraSm;
}

} // namespace

