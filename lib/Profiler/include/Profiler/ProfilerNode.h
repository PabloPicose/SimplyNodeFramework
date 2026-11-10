#pragma once
#include "TraceEvent.h"
#include "MemoryTracker.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

/// Maximum number of events in one "spans" message.
#ifndef PROFILER_CHUNK_SIZE
#define PROFILER_CHUNK_SIZE 1024
#endif

/// How often the ring buffers are drained and sent, in milliseconds.
#ifndef PROFILER_FLUSH_INTERVAL_MS
#define PROFILER_FLUSH_INTERVAL_MS 100
#endif

namespace snf::profiler {

/**
 * @brief Drains profiler buffers and serializes them for the dashboard.
 */
class ProfilerNode : public snf::Node {
public:
    /// Creates a profiler node attached to @p parent.
    explicit ProfilerNode(snf::Node* parent = nullptr);
    ~ProfilerNode() override;

    void update() override {}

    /// Drains every ring buffer and emits the events as "spans" messages of at
    /// most PROFILER_CHUNK_SIZE events, followed by one "mem" message.
    /// Runs every PROFILER_FLUSH_INTERVAL_MS.
    void drainAndProcess();

    /// Pre-serialized JSON messages consumed by ProfilerServer.
    snf::Signal<std::string> broadcastMessage;

private:
    std::vector<TraceEvent> m_events;  ///< Reused drain buffer.
    std::ofstream           m_traceFile;
    bool                    m_fileOpen{false};
    std::string             m_traceFilePath;
};

} // namespace snf::profiler
