#pragma once
#include "TraceEvent.h"
#include "MemoryTracker.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#ifndef PROFILER_CHUNK_SIZE
#define PROFILER_CHUNK_SIZE 256
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

    /// Ingests events from ring buffers and flushes serialized output.
    void drainAndProcess();

    /// Pre-serialized JSON messages consumed by ProfilerServer.
    snf::Signal<std::string> broadcastMessage;

private:
    void flushChunk();
    std::string serializeEvent(const TraceEvent& e) const;
    std::string serializeMemSnapshot(uint64_t ts_ns) const;

    std::vector<TraceEvent> m_chunk;
    std::ofstream           m_traceFile;
    bool                    m_fileOpen{false};
    std::string             m_traceFilePath;
};

} // namespace snf::profiler
