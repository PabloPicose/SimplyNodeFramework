#pragma once
#include <cstddef>
#include <cstdint>

namespace snf::profiler {

/**
 * @brief Event phase encoded in the profiler trace stream.
 */
enum class EventPhase : uint8_t { BEGIN, END, INSTANT, ALLOC, FREE };

/**
 * @brief Single trace or memory event emitted by the profiler runtime.
 *
 * The category and name pointers reference string literals and are not owned
 * by the event.
 */
struct TraceEvent {
    uint64_t   timestamp_ns;
    uint32_t   thread_id;
    EventPhase phase;
    const char* category;      ///< Category string literal, not owned.
    const char* name;          ///< Name string literal, not owned.
    size_t     payload_bytes;  ///< Allocation size for memory events.
};

} // namespace snf::profiler
