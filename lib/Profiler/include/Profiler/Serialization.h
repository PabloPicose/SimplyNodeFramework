#pragma once
#include "MemoryTracker.h"
#include "SysMonitor.h"
#include "TraceEvent.h"
#include <cstddef>
#include <cstdint>
#include <string>

/**
 * @brief JSON encoding of the profiler WebSocket stream.
 *
 * Every "ts" is a steady_clock timestamp in nanoseconds.
 */
namespace snf::profiler::detail {

/// One trace event as a JSON object (an element of a "spans" message).
std::string serializeEvent(const TraceEvent& e);

/// @p count events as one "spans" message: {"type":"spans","events":[...]}.
std::string serializeEventBatch(const TraceEvent* events, size_t count);

/// Heap usage as a "mem" message.
std::string serializeMemSnapshot(const MemSnapshot& snap, uint64_t ts_ns);

/// Host CPU, RAM and network usage as a "sys" message.
std::string serializeSysSample(const SysSample& s);

} // namespace snf::profiler::detail
