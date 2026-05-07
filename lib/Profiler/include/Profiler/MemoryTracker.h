#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace snf::profiler {

struct MemSnapshot {
    size_t live_count;
    size_t live_bytes;
    size_t peak_bytes;
};

class MemoryTracker {
public:
    static void enable();    // called by SNF_PROFILER_INIT
    static void disable();   // called by SNF_PROFILER_SHUTDOWN
    static bool isEnabled();

    static void recordAlloc(void* ptr, size_t size);
    static void recordFree(void* ptr);

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
