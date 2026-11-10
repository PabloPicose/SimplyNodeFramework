#pragma once
#include "TraceEvent.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#ifndef PROFILER_BUFFER_SIZE
#define PROFILER_BUFFER_SIZE 65536
#endif

namespace snf::profiler {

/**
 * @brief Lock-free-ish ring buffer used to collect profiler events per thread.
 *
 * Each thread writes into its own current buffer and the profiler node drains
 * the buffers periodically for serialization and broadcast.
 */
class TraceBuffer {
public:
    /// Maximum number of trace events stored in one buffer.
    static constexpr uint32_t kCapacity = PROFILER_BUFFER_SIZE;

    TraceBuffer();
    ~TraceBuffer();

    /// Pushes one event into the buffer.
    bool push(const TraceEvent& e) noexcept;

    /// Moves all buffered events into @p out.
    void drain(std::vector<TraceEvent>& out);

    /// Returns the thread-local buffer associated with the current thread.
    static TraceBuffer& current();

    /// Drains every registered thread-local buffer into @p out.
    static void drainAll(std::vector<TraceEvent>& out);

    /// Returns how many events push() rejected since the last call, and resets it.
    uint64_t takeDropped() noexcept;

    /// takeDropped() over every buffer, including those of threads that exited.
    static uint64_t takeDroppedAll();

private:
    std::atomic<uint32_t> m_head{0};
    std::atomic<uint32_t> m_tail{0};
    std::atomic<uint64_t> m_dropped{0};
    std::array<TraceEvent, kCapacity> m_data{};
};

} // namespace snf::profiler
