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

class ProfilerNode : public snf::Node {
public:
    explicit ProfilerNode(snf::Node* parent = nullptr);
    ~ProfilerNode() override;

    void update() override {}

    // Ingests events from ring buffers; called by the drain timer
    void drainAndProcess();

    // Signals consumed by ProfilerServer
    snf::Signal<std::string> broadcastMessage;  // pre-serialized JSON string

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
