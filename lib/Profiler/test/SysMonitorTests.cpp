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

    Application* m_app = nullptr;
};

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

