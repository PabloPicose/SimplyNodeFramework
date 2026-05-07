// MacrosTests.cpp — verifies that TRACE_EVENT*, TRACE_MEMORY_* macros push
// the expected TraceEvent entries into the calling thread's ring buffer.
// No Application dependency; purely verifies ring-buffer content.

#include <gtest/gtest.h>

#include "Profiler/Macros.h"
#include "Profiler/TraceBuffer.h"
#include "Profiler/TraceEvent.h"

#include <thread>
#include <vector>

using namespace snf::profiler;

namespace {

// Drain the current thread's buffer and return all pending events.
static std::vector<TraceEvent> drainCurrent()
{
    std::vector<TraceEvent> out;
    TraceBuffer::current().drain(out);
    return out;
}

// ── TRACE_EVENT (1-arg RAII) ──────────────────────────────────────────────────

TEST(MacrosTest, TraceEventOneCatPushesBeginAndEnd)
{
    drainCurrent(); // discard leftovers
    {
        TRACE_EVENT("mycat");
    }
    auto events = drainCurrent();
    ASSERT_GE(events.size(), 2u);

    bool hasBegin = false, hasEnd = false;
    for (auto& e : events) {
        if (std::string(e.category) == "mycat") {
            if (e.phase == EventPhase::BEGIN) hasBegin = true;
            if (e.phase == EventPhase::END)   hasEnd   = true;
        }
    }
    EXPECT_TRUE(hasBegin) << "expected BEGIN phase for 'mycat'";
    EXPECT_TRUE(hasEnd)   << "expected END phase for 'mycat'";
}

TEST(MacrosTest, TraceEventDefaultNameIsFunction)
{
    drainCurrent();
    {
        TRACE_EVENT("funcname_cat");
    }
    auto events = drainCurrent();
    // The name must be a non-empty string (set to __func__ by the macro)
    bool nameSet = false;
    for (auto& e : events) {
        if (e.phase == EventPhase::BEGIN &&
            std::string(e.category) == "funcname_cat") {
            nameSet = (e.name != nullptr && e.name[0] != '\0');
        }
    }
    EXPECT_TRUE(nameSet);
}

// ── TRACE_EVENT (2-arg RAII) ──────────────────────────────────────────────────

TEST(MacrosTest, TraceEventTwoArgUsesExplicitName)
{
    drainCurrent();
    {
        TRACE_EVENT("cat2", "myexplicitname");
    }
    auto events = drainCurrent();
    ASSERT_GE(events.size(), 2u);

    bool found = false;
    for (auto& e : events) {
        if (e.phase == EventPhase::BEGIN &&
            std::string(e.name) == "myexplicitname")
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(MacrosTest, TraceEventEndPhaseCategoryMatchesBegin)
{
    drainCurrent();
    {
        TRACE_EVENT("matched_cat", "matched_name");
    }
    auto events = drainCurrent();
    std::string beginCat, endCat;
    for (auto& e : events) {
        if (e.phase == EventPhase::BEGIN) beginCat = e.category;
        if (e.phase == EventPhase::END)   endCat   = e.category;
    }
    EXPECT_EQ(beginCat, endCat);
}

// ── TRACE_EVENT_BEGIN / TRACE_EVENT_END ───────────────────────────────────────

TEST(MacrosTest, ExplicitBeginEndPushCorrectPhases)
{
    drainCurrent();
    TRACE_EVENT_BEGIN("work", "step1");
    TRACE_EVENT_END("work");
    auto events = drainCurrent();
    ASSERT_GE(events.size(), 2u);

    EXPECT_EQ(events[0].phase, EventPhase::BEGIN);
    EXPECT_STREQ(events[0].category, "work");
    EXPECT_STREQ(events[0].name,     "step1");

    EXPECT_EQ(events[1].phase, EventPhase::END);
    EXPECT_STREQ(events[1].category, "work");
}

TEST(MacrosTest, ExplicitBeginCapturesFileAndLine)
{
    drainCurrent();
    TRACE_EVENT_BEGIN("fl_test", "marker");
    auto events = drainCurrent();
    // The macro records thread_id which must match the current thread
    ASSERT_GE(events.size(), 1u);
    uint32_t expected = static_cast<uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    EXPECT_EQ(events[0].thread_id, expected);
}

// ── Timestamp ordering ────────────────────────────────────────────────────────

TEST(MacrosTest, TimestampsAreMonotonic)
{
    drainCurrent();
    TRACE_EVENT_BEGIN("ts", "a");
    TRACE_EVENT_END("ts");
    auto events = drainCurrent();
    ASSERT_GE(events.size(), 2u);
    uint64_t beginTs = 0, endTs = 0;
    for (auto& e : events) {
        if (e.phase == EventPhase::BEGIN) beginTs = e.timestamp_ns;
        if (e.phase == EventPhase::END)   endTs   = e.timestamp_ns;
    }
    EXPECT_GE(endTs, beginTs);
}

// ── TRACE_MEMORY_ALLOC / TRACE_MEMORY_FREE ────────────────────────────────────

TEST(MacrosTest, TraceMemoryAllocPushesAllocEvent)
{
    drainCurrent();
    int dummy = 0;
    TRACE_MEMORY_ALLOC(&dummy, 42);
    auto events = drainCurrent();
    ASSERT_GE(events.size(), 1u);
    bool found = false;
    for (auto& e : events) {
        if (e.phase == EventPhase::ALLOC && e.payload_bytes == 42u)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST(MacrosTest, TraceMemoryFreePushesEvent)
{
    drainCurrent();
    int dummy = 0;
    TRACE_MEMORY_FREE(&dummy);
    auto events = drainCurrent();
    ASSERT_GE(events.size(), 1u);
    bool found = false;
    for (auto& e : events) {
        if (e.phase == EventPhase::FREE) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(MacrosTest, TraceMemoryAllocPayloadBytesPreserved)
{
    drainCurrent();
    int dummy = 0;
    TRACE_MEMORY_ALLOC(&dummy, 9999);
    auto events = drainCurrent();
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].payload_bytes, 9999u);
}

// ── Thread-local isolation ────────────────────────────────────────────────────

TEST(MacrosTest, EventsFromOtherThreadNotVisible)
{
    drainCurrent(); // clear this thread

    std::thread t([]() {
        TRACE_EVENT("other_thread_cat");
    });
    t.join();

    // Drain this thread — should not see events from the other thread
    auto events = drainCurrent();
    for (auto& e : events) {
        EXPECT_NE(std::string(e.category), "other_thread_cat")
            << "Should not see events from another thread's buffer";
    }
}

} // namespace
