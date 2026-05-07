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

class TraceBuffer {
public:
    static constexpr uint32_t kCapacity = PROFILER_BUFFER_SIZE;

    TraceBuffer();
    ~TraceBuffer();

    bool push(const TraceEvent& e) noexcept;
    void drain(std::vector<TraceEvent>& out);

    static TraceBuffer& current();
    static void drainAll(std::vector<TraceEvent>& out);

private:
    std::atomic<uint32_t> m_head{0};
    std::atomic<uint32_t> m_tail{0};
    std::array<TraceEvent, kCapacity> m_data{};
};

} // namespace snf::profiler
