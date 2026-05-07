// TraceBufferTests.cpp — unit tests for the SPSC ring buffer
// No Application dependency; pure component tests.

#include <gtest/gtest.h>

#include "Profiler/TraceBuffer.h"
#include "Profiler/TraceEvent.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace snf::profiler;

namespace {

TraceEvent makeEvent(EventPhase ph,
                     const char* cat = "test",
                     const char* name = "fn",
                     uint64_t ts = 0)
{
    TraceEvent e{};
    e.timestamp_ns  = ts;
    e.thread_id     = 1;
    e.phase         = ph;
    e.category      = cat;
    e.name          = name;
    e.payload_bytes = 0;
    return e;
}

// ── Single-buffer tests ───────────────────────────────────────────────────────

TEST(TraceBufferTest, DrainEmptyReturnsNothing)
{
    TraceBuffer buf;
    std::vector<TraceEvent> out;
    buf.drain(out);
    EXPECT_TRUE(out.empty());
}

TEST(TraceBufferTest, PushThenDrainRecoversEvent)
{
    TraceBuffer buf;
    EXPECT_TRUE(buf.push(makeEvent(EventPhase::BEGIN)));
    std::vector<TraceEvent> out;
    buf.drain(out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].phase, EventPhase::BEGIN);
    EXPECT_STREQ(out[0].category, "test");
    EXPECT_STREQ(out[0].name, "fn");
}

TEST(TraceBufferTest, PushMultipleEventsPreservesOrder)
{
    TraceBuffer buf;
    for (int i = 0; i < 10; ++i) {
        auto e = makeEvent(EventPhase::INSTANT, "cat", "n",
                           static_cast<uint64_t>(i));
        buf.push(e);
    }
    std::vector<TraceEvent> out;
    buf.drain(out);
    ASSERT_EQ(out.size(), 10u);
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(out[i].timestamp_ns, static_cast<uint64_t>(i));
}

TEST(TraceBufferTest, DrainClearsBuffer)
{
    TraceBuffer buf;
    buf.push(makeEvent(EventPhase::BEGIN));
    std::vector<TraceEvent> out;
    buf.drain(out);
    ASSERT_EQ(out.size(), 1u);
    out.clear();
    buf.drain(out);
    EXPECT_TRUE(out.empty());
}

TEST(TraceBufferTest, BufferFullDropsExtraEvents)
{
    TraceBuffer buf;
    int pushed = 0;
    // Ring buffer holds at most kCapacity-1 items (head==tail means empty)
    for (uint32_t i = 0; i < TraceBuffer::kCapacity + 10; ++i) {
        if (buf.push(makeEvent(EventPhase::INSTANT))) ++pushed;
    }
    EXPECT_LT(pushed, static_cast<int>(TraceBuffer::kCapacity) + 10);
    std::vector<TraceEvent> out;
    buf.drain(out);
    EXPECT_EQ(out.size(), static_cast<size_t>(pushed));
}

TEST(TraceBufferTest, AllPhasesRoundTripCorrectly)
{
    TraceBuffer buf;
    const EventPhase phases[] = {
        EventPhase::BEGIN, EventPhase::END,
        EventPhase::INSTANT, EventPhase::ALLOC, EventPhase::FREE
    };
    for (auto ph : phases)
        buf.push(makeEvent(ph));

    std::vector<TraceEvent> out;
    buf.drain(out);
    ASSERT_EQ(out.size(), 5u);
    for (size_t i = 0; i < 5; ++i)
        EXPECT_EQ(out[i].phase, phases[i]);
}

TEST(TraceBufferTest, PayloadBytesPreserved)
{
    TraceBuffer buf;
    auto e = makeEvent(EventPhase::ALLOC);
    e.payload_bytes = 4096;
    buf.push(e);
    std::vector<TraceEvent> out;
    buf.drain(out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[out.size()-1].payload_bytes, 4096u);
}

// ── Cross-thread tests ────────────────────────────────────────────────────────

TEST(TraceBufferTest, CurrentReturnsDifferentBufferPerThread)
{
    TraceBuffer* mainBuf = &TraceBuffer::current();
    TraceBuffer* threadBuf = nullptr;

    std::thread t([&]() { threadBuf = &TraceBuffer::current(); });
    t.join();

    EXPECT_NE(mainBuf, threadBuf);
}

TEST(TraceBufferTest, CurrentReturnsSameBufferOnSameThread)
{
    EXPECT_EQ(&TraceBuffer::current(), &TraceBuffer::current());
}

TEST(TraceBufferTest, DrainAllCollectsFromMultipleThreads)
{
    // Drain any leftover events from the test thread itself before the test.
    { std::vector<TraceEvent> discard; TraceBuffer::drainAll(discard); }

    constexpr int kThreads          = 4;
    constexpr int kEventsPerThread  = 8;

    std::atomic<int>  written{0};  // counts threads that finished writing
    std::atomic<bool> drainDone{false}; // signals threads they can exit

    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kEventsPerThread; ++i) {
                auto e = makeEvent(EventPhase::INSTANT, "cat", "n",
                                   static_cast<uint64_t>(t * 100 + i));
                e.thread_id = static_cast<uint32_t>(t);
                TraceBuffer::current().push(e);
            }
            // Signal that this thread has pushed all events.
            written.fetch_add(1);
            // Wait until the main thread has drained before exiting so that
            // the thread-local buffer is not destroyed before drainAll().
            while (!drainDone.load()) std::this_thread::yield();
        });
    }

    // Wait until all threads have written.
    while (written.load() < kThreads) std::this_thread::yield();

    // Now drain — all worker thread-local buffers are still alive.
    std::vector<TraceEvent> out;
    TraceBuffer::drainAll(out);

    // Release the threads so they can exit.
    drainDone.store(true);
    for (auto& th : threads) th.join();

    EXPECT_GE(out.size(), static_cast<size_t>(kThreads * kEventsPerThread));
}

TEST(TraceBufferTest, DrainAllOnEmptyIsHarmless)
{
    std::vector<TraceEvent> out;
    // Any pre-existing events from the test thread are collected — that is fine.
    TraceBuffer::drainAll(out);
    // Call again — must not crash and must return 0 new items.
    size_t countAfterFirst = out.size();
    out.clear();
    TraceBuffer::drainAll(out);
    EXPECT_EQ(out.size(), 0u);
    (void)countAfterFirst;
}

} // namespace
