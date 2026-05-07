#pragma once
#include <cstddef>
#include <cstdint>

namespace snf::profiler {

enum class EventPhase : uint8_t { BEGIN, END, INSTANT, ALLOC, FREE };

struct TraceEvent {
    uint64_t    timestamp_ns;
    uint32_t    thread_id;
    EventPhase  phase;
    const char* category;       // points to string literal — not owned
    const char* name;           // points to string literal — not owned
    size_t      payload_bytes;  // alloc/free size; 0 for trace events
};

} // namespace snf::profiler
