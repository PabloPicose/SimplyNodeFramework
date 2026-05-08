#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

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
 * @brief Tracks heap allocations while profiling is enabled.
 */
class MemoryTracker {
public:
    /// Enables allocation tracking.
    static void enable();

    /// Disables allocation tracking.
    static void disable();

    /// Returns whether tracking is currently active.
    static bool isEnabled();

    /// Records a heap allocation.
    static void recordAlloc(void* ptr, size_t size);

    /// Records a heap deallocation.
    static void recordFree(void* ptr);

    /// Returns the current memory usage snapshot.
    static MemSnapshot snapshot();

private:
    MemoryTracker() = default;

    std::mutex m_mutex;
    std::unordered_map<void*, size_t> m_map;
    size_t m_live_bytes{0};
    size_t m_live_count{0};
    size_t m_peak_bytes{0};
};

} // namespace snf::profiler
