#pragma once
#include "MemoryTracker.h"
#include "SysMonitor.h"
#include "TraceEvent.h"
#include <cstdint>
#include <string>

/**
 * @brief JSON encoding of the profiler WebSocket stream.
 *
 * Every "ts" is a steady_clock timestamp in nanoseconds.
 */
namespace snf::profiler::detail {

/// One trace event as a "span" message.
std::string serializeEvent(const TraceEvent& e);

/// Heap usage as a "mem" message.
std::string serializeMemSnapshot(const MemSnapshot& snap, uint64_t ts_ns);

/// Host CPU, RAM and network usage as a "sys" message.
std::string serializeSysSample(const SysSample& s);

} // namespace snf::profiler::detail
