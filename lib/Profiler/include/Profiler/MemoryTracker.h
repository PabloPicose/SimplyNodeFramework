#pragma once
#include <cstddef>
#include <cstdint>

namespace snf::profiler {

/**
 * @brief Current memory usage snapshot gathered by the profiler.
 */
struct MemSnapshot {
    size_t live_count;
    size_t live_bytes;
    size_t peak_bytes;
};

/**
 * @brief Tracks heap allocations made through operator new while enabled.
 *
 * Linking SNFProfiler replaces the global operator new/delete. Every block
 * carries a 16-byte header with its size and the tracking session it was
 * allocated in, so counting needs no lock and no lookup table. A block
 * allocated while tracking was disabled, or in an earlier session, is not
 * subtracted when freed.
 *
 * live_count and live_bytes are exact. peak_bytes is refreshed on every
 * snapshot() and whenever a thread's allocations grow by 64 KB, so a spike
 * shorter than that may be underestimated by up to 64 KB per thread.
 */
class MemoryTracker {
public:
    /// Starts a new tracking session with zeroed counters (no-op if enabled).
    static void enable();

    /// Stops tracking. Safe to call while other threads allocate.
    static void disable();

    /// Returns whether tracking is currently active.
    static bool isEnabled();

    /// Returns the current session's counters, or zeros when disabled.
    static MemSnapshot snapshot();

private:
    MemoryTracker() = delete;
};

} // namespace snf::profiler
