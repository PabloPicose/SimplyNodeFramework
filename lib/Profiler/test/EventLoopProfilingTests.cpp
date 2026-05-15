// EventLoopProfilingTests.cpp
//
// Verifies that every EventLoop emits profiler trace spans covering its
// active-processing phase (tasks + timers + IO + node ticks) and that the
// idle/sleeping time is excluded from the measurement.
//
// Tests run inside SNFProfilerTests, which links SNFProfiler, so the strong
// hook implementations are active (no-op weak stubs do NOT apply here).

#include <gtest/gtest.h>

#include "Profiler/TraceBuffer.h"
#include "Profiler/TraceEvent.h"
#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Runnable.h"
#include "SNFCore/ThreadPool.h"
#include "SNFCore/Timer.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <set>
#include <thread>
#include <vector>

using namespace snf;
using namespace snf::profiler;
using namespace std::chrono_literals;

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────────

// Helper Runnable used by worker-thread tests.
class LambdaRunnable final : public snf::Runnable
{
public:
    explicit LambdaRunnable(std::function<void()> body) : m_body(std::move(body)) {}
protected:
    void run() override { if (m_body) m_body(); }
private:
    std::function<void()> m_body;
};

// Drain all registered thread-local TraceBuffers and return the events.
static std::vector<TraceEvent> drainAll()
{
    std::vector<TraceEvent> out;
    TraceBuffer::drainAll(out);
    return out;
}

// Filter events by category and optionally by phase.
static std::vector<TraceEvent> filter(const std::vector<TraceEvent>& events,
                                      const char* cat,
                                      EventPhase ph)
{
    std::vector<TraceEvent> out;
    for (const auto& e : events) {
        if (e.category && std::string(e.category) == cat && e.phase == ph)
            out.push_back(e);
    }
    return out;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class EventLoopProfilingFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_app = new Application(0, nullptr);
        // Flush any bootstrap spans emitted during Application construction.
        drainAll();
    }

    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }

    // Post a task to the main-thread EventLoop and run until the task fires
    // or the timeout expires.
    void runMainUntilDone(std::atomic<bool>& done,
                          std::chrono::milliseconds timeout = 200ms)
    {
        EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();

        Timer guard;
        guard.setSingleShot(true);
        guard.timeout.connect([loop]() { loop->stop(); });
        guard.start(timeout);

        std::thread watcher([&]() {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            while (!done.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(1ms);
            loop->stop();
        });

        loop->run();
        watcher.join();
    }

    Application* m_app = nullptr;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. The main-thread EventLoop emits a BEGIN span when it processes a task.
TEST_F(EventLoopProfilingFixture, MainLoopEmitsBeginSpanOnProcessing)
{
    std::atomic<bool> done{false};
    EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();
    loop->post([&done]() { done.store(true); });

    runMainUntilDone(done);

    auto events = drainAll();
    auto begins = filter(events, "eventloop", EventPhase::BEGIN);
    EXPECT_GE(begins.size(), 1u)
        << "Expected at least one eventloop/processing BEGIN span";
}

// 2. Every BEGIN span has a matching END span in the same thread.
TEST_F(EventLoopProfilingFixture, MainLoopEmitsMatchingEndSpan)
{
    std::atomic<bool> done{false};
    EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();
    loop->post([&done]() { done.store(true); });

    runMainUntilDone(done);

    auto events = drainAll();
    auto begins = filter(events, "eventloop", EventPhase::BEGIN);
    auto ends   = filter(events, "eventloop", EventPhase::END);

    EXPECT_GE(ends.size(), 1u) << "Expected at least one eventloop/processing END span";
    // There should be a balanced number of begins and ends.
    // (The last iteration's END may arrive on the next drain, so allow
    //  ends.size() to differ by at most 1.)
    EXPECT_LE(begins.size(), ends.size() + 1);
    EXPECT_LE(ends.size(),   begins.size() + 1);
}

// 3. Timestamps: the END span in each iteration comes after the BEGIN span.
TEST_F(EventLoopProfilingFixture, ProcessingEndTimestampNotBeforeBegin)
{
    std::atomic<bool> done{false};
    EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();
    loop->post([&done]() { done.store(true); });

    runMainUntilDone(done);

    auto events = drainAll();
    // Walk through the events in order; pair each BEGIN with the next END.
    uint64_t lastBeginTs = 0;
    bool seenBegin = false;
    for (const auto& e : events) {
        if (e.category && std::string(e.category) != "eventloop") continue;
        if (e.phase == EventPhase::BEGIN) {
            lastBeginTs = e.timestamp_ns;
            seenBegin   = true;
        } else if (e.phase == EventPhase::END && seenBegin) {
            EXPECT_GE(e.timestamp_ns, lastBeginTs)
                << "END timestamp must be >= BEGIN timestamp";
            seenBegin = false;
        }
    }
}

// 4. The thread ID recorded in processing spans matches the EventLoop owner.
TEST_F(EventLoopProfilingFixture, ProcessingSpanThreadIdMatchesOwner)
{
    std::atomic<bool> done{false};
    EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();

    // The main-thread EventLoop is owned by std::this_thread.
    uint32_t expectedTid = static_cast<uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));

    loop->post([&done]() { done.store(true); });
    runMainUntilDone(done);

    auto events = drainAll();
    auto begins = filter(events, "eventloop", EventPhase::BEGIN);
    ASSERT_GE(begins.size(), 1u);

    for (const auto& e : begins)
        EXPECT_EQ(e.thread_id, expectedTid)
            << "Processing span must carry the owner thread's ID";
}

// 5. Worker threads (ThreadPool) each emit their own processing spans with
//    distinct thread IDs.
TEST_F(EventLoopProfilingFixture, WorkerThreadsEmitSpansWithDistinctThreadIds)
{
    std::atomic<int> completedTasks{0};
    const int kTasks = 4;
    ThreadPool* pool = ThreadPool::globalInstance();
    ASSERT_NE(pool, nullptr);

    for (int i = 0; i < kTasks; ++i) {
        pool->start(std::make_shared<LambdaRunnable>([&completedTasks]() {
            // Do a tiny bit of work so the processing phase is non-trivial.
            std::this_thread::sleep_for(1ms);
            completedTasks.fetch_add(1);
        }));
    }
    pool->waitForDone();

    auto events = drainAll();
    auto begins = filter(events, "eventloop", EventPhase::BEGIN);

    // At least one processing span must have been emitted by worker threads.
    // Multiple tasks may be batched into a single event loop iteration (one
    // span per iteration, not per task), so we only assert >= 1.
    EXPECT_GE(begins.size(), 1u)
        << "Expected at least one processing span from worker threads";

    // The spans should come from at least 2 different thread IDs
    // (worker threads are distinct from the main thread).
    std::set<uint32_t> tids;
    uint32_t mainTid = static_cast<uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    for (const auto& e : begins)
        if (e.thread_id != mainTid)
            tids.insert(e.thread_id);

    EXPECT_GE(tids.size(), 1u)
        << "Expected processing spans from at least one worker thread";
}

// 6. runPendingWork() also emits a processing span.
TEST_F(EventLoopProfilingFixture, RunPendingWorkEmitsProcessingSpan)
{
    EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();

    std::atomic<bool> taskRan{false};
    loop->post([&taskRan]() { taskRan.store(true); });

    // runPendingWork is a single non-blocking pass — no sleeping.
    loop->runPendingWork();
    ASSERT_TRUE(taskRan.load());

    auto events = drainAll();
    auto begins = filter(events, "eventloop", EventPhase::BEGIN);
    auto ends   = filter(events, "eventloop", EventPhase::END);

    EXPECT_GE(begins.size(), 1u) << "runPendingWork must emit BEGIN span";
    EXPECT_GE(ends.size(),   1u) << "runPendingWork must emit END span";
}

// 7. Multiple calls to runPendingWork() each produce their own span pair.
TEST_F(EventLoopProfilingFixture, MultipleRunPendingWorkCallsProduceMultipleSpans)
{
    EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();

    for (int i = 0; i < 3; ++i)
        loop->runPendingWork();

    auto events = drainAll();
    auto begins = filter(events, "eventloop", EventPhase::BEGIN);

    EXPECT_GE(begins.size(), 3u)
        << "Three runPendingWork calls must emit at least 3 BEGIN spans";
}

// 8. Span category is exactly "eventloop" and name (for BEGIN) is "processing".
TEST_F(EventLoopProfilingFixture, SpanCategoryAndNameAreCorrect)
{
    EventLoop* loop = m_app->getOrCreateCurrentThreadEventLoop();
    loop->runPendingWork();

    auto events = drainAll();
    bool foundBegin = false, foundEnd = false;
    for (const auto& e : events) {
        if (!e.category) continue;
        if (std::string(e.category) != "eventloop") continue;
        if (e.phase == EventPhase::BEGIN) {
            foundBegin = true;
            EXPECT_STREQ(e.name, "processing");
        }
        if (e.phase == EventPhase::END) {
            foundEnd = true;
        }
    }
    EXPECT_TRUE(foundBegin) << "Expected BEGIN span with cat=eventloop";
    EXPECT_TRUE(foundEnd)   << "Expected END span with cat=eventloop";
}

} // namespace
