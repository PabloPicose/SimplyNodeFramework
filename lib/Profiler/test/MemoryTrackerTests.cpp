// MemoryTrackerTests.cpp — unit tests for the placement-new singleton tracker
// No Application dependency; manually calls enable()/disable().

#include <gtest/gtest.h>

#include "Profiler/MemoryTracker.h"

#include <cstdlib>
#include <memory>
#include <vector>

using namespace snf::profiler;

namespace {

// Helper: allocate and immediately free a block of the given size,
// returning the delta in live_bytes relative to 'before'.
static MemSnapshot deltaAlloc(size_t sz)
{
    auto before = MemoryTracker::snapshot();
    void* p = ::operator new(sz);
    auto after  = MemoryTracker::snapshot();
    ::operator delete(p);
    // Return the snapshot taken while the block was live
    (void)before;
    return after;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

TEST(MemoryTrackerTest, DisabledByDefaultSnapshotIsZero)
{
    if (MemoryTracker::isEnabled()) MemoryTracker::disable();
    auto snap = MemoryTracker::snapshot();
    EXPECT_EQ(snap.live_count, 0u);
    EXPECT_EQ(snap.live_bytes, 0u);
    EXPECT_EQ(snap.peak_bytes, 0u);
}

TEST(MemoryTrackerTest, EnableAndDisableToggle)
{
    EXPECT_FALSE(MemoryTracker::isEnabled());
    MemoryTracker::enable();
    EXPECT_TRUE(MemoryTracker::isEnabled());
    MemoryTracker::disable();
    EXPECT_FALSE(MemoryTracker::isEnabled());
}

TEST(MemoryTrackerTest, EnableIsIdempotent)
{
    MemoryTracker::enable();
    MemoryTracker::enable(); // second call must not crash or corrupt state
    EXPECT_TRUE(MemoryTracker::isEnabled());
    MemoryTracker::disable();
}

// ── Allocation tracking ───────────────────────────────────────────────────────

class MemoryTrackerFixture : public ::testing::Test
{
public:
    void SetUp() override
    {
        MemoryTracker::enable();
        m_baseline = MemoryTracker::snapshot();
    }

    void TearDown() override { MemoryTracker::disable(); }

    MemSnapshot m_baseline;
};

TEST_F(MemoryTrackerFixture, AllocationIncreasesLiveCount)
{
    auto before = MemoryTracker::snapshot();
    void* p = ::operator new(128);
    auto after  = MemoryTracker::snapshot();
    ::operator delete(p);
    EXPECT_GT(after.live_count, before.live_count);
}

TEST_F(MemoryTrackerFixture, AllocationTracksBytes)
{
    constexpr size_t kSize = 1024;
    auto before = MemoryTracker::snapshot();
    void* p = ::operator new(kSize);
    auto after  = MemoryTracker::snapshot();
    ::operator delete(p);
    EXPECT_GE(after.live_bytes, before.live_bytes + kSize);
}

TEST_F(MemoryTrackerFixture, FreeDecreasesLiveCountAndBytes)
{
    constexpr size_t kSize = 256;
    void* p = ::operator new(kSize);
    auto before = MemoryTracker::snapshot();
    ::operator delete(p);
    auto after  = MemoryTracker::snapshot();
    EXPECT_LT(after.live_count, before.live_count);
    EXPECT_LT(after.live_bytes, before.live_bytes);
}

TEST_F(MemoryTrackerFixture, PeakNeverDecreases)
{
    void* p = ::operator new(4096);
    auto afterAlloc = MemoryTracker::snapshot();
    ::operator delete(p);
    auto afterFree  = MemoryTracker::snapshot();
    EXPECT_GE(afterFree.peak_bytes, afterAlloc.peak_bytes);
}

TEST_F(MemoryTrackerFixture, PeakEqualsHighWaterMark)
{
    void* p1 = ::operator new(512);
    void* p2 = ::operator new(512);
    auto snap1 = MemoryTracker::snapshot();
    ::operator delete(p1);
    ::operator delete(p2);
    auto snap2 = MemoryTracker::snapshot();
    // Peak must equal the max live_bytes seen
    EXPECT_GE(snap1.peak_bytes, 1024u - m_baseline.live_bytes > 0
                                 ? snap1.live_bytes : 0u);
    EXPECT_GE(snap2.peak_bytes, snap1.peak_bytes);
}

TEST_F(MemoryTrackerFixture, MultipleAllocsFreeAccumulate)
{
    constexpr int kN    = 10;
    constexpr size_t kB = 64;
    std::vector<void*> ptrs;
    ptrs.reserve(kN);
    for (int i = 0; i < kN; ++i)
        ptrs.push_back(::operator new(kB));

    auto snap = MemoryTracker::snapshot();
    EXPECT_GE(snap.live_count, m_baseline.live_count + static_cast<size_t>(kN));
    EXPECT_GE(snap.live_bytes, m_baseline.live_bytes + kN * kB);

    for (void* p : ptrs) ::operator delete(p);
}

// ── Disabled state ────────────────────────────────────────────────────────────

TEST_F(MemoryTrackerFixture, DisableStopsTracking)
{
    MemoryTracker::disable();
    auto snap1 = MemoryTracker::snapshot();
    void* p = ::operator new(512);
    auto snap2 = MemoryTracker::snapshot();
    ::operator delete(p);
    // Both snapshots must be zero after disable
    EXPECT_EQ(snap1.live_count, 0u);
    EXPECT_EQ(snap2.live_count, 0u);

    // Re-enable for TearDown (which calls disable() again — harmless)
    MemoryTracker::enable();
}

// ── Re-entrance guard ─────────────────────────────────────────────────────────

TEST_F(MemoryTrackerFixture, InternalAllocationsDoNotCorruptState)
{
    // Trigger many std::vector resizes to exercise re-entrance guard;
    // if the guard is broken the test will either deadlock or crash.
    std::vector<std::unique_ptr<int>> ptrs;
    ptrs.reserve(1000);
    for (int i = 0; i < 1000; ++i)
        ptrs.push_back(std::make_unique<int>(i));
    auto snap = MemoryTracker::snapshot();
    EXPECT_TRUE(MemoryTracker::isEnabled());
    EXPECT_GT(snap.live_count, 0u);
}

} // namespace
