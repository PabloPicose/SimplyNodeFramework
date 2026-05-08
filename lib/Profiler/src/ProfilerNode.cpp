#include "Profiler/ProfilerNode.h"
#include "Profiler/ProfilerServer.h"
#include "Profiler/SysMonitor.h"
#include "Profiler/TraceBuffer.h"
#include "SNFCore/Timer.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

namespace snf::profiler {

ProfilerNode::ProfilerNode(snf::Node* parent)
    : snf::Node(parent)
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    uint64_t ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
    char path[64];
    std::snprintf(path, sizeof(path), "snf_profile_%llu.json", (unsigned long long)ts);
    m_traceFilePath = path;
    m_traceFile.open(m_traceFilePath, std::ios::app);
    m_fileOpen = m_traceFile.is_open();

    auto* timer = new snf::Timer(this);
    timer->timeout.connect([this]() { drainAndProcess(); });
    timer->start(std::chrono::milliseconds(16));
    moveToThreadPool();
}

ProfilerNode::~ProfilerNode() {
    if (m_traceFile.is_open())
        m_traceFile.close();
}

void ProfilerNode::drainAndProcess() {
    std::vector<TraceEvent> tmp;
    TraceBuffer::drainAll(tmp);
    m_chunk.insert(m_chunk.end(), tmp.begin(), tmp.end());
    if (m_chunk.size() >= PROFILER_CHUNK_SIZE)
        flushChunk();
}

void ProfilerNode::flushChunk() {
    uint64_t ts_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    for (const auto& e : m_chunk) {
        std::string json = serializeEvent(e);
        if (m_fileOpen)
            m_traceFile << json << '\n';
        broadcastMessage.emit(json);
    }

    // Emit memory snapshot
    std::string memJson = serializeMemSnapshot(ts_ns);
    if (m_fileOpen)
        m_traceFile << memJson << '\n';
    broadcastMessage.emit(memJson);

    m_chunk.clear();
}

std::string ProfilerNode::serializeEvent(const TraceEvent& e) const {
    char ph;
    switch (e.phase) {
        case EventPhase::BEGIN:   ph = 'B'; break;
        case EventPhase::END:     ph = 'E'; break;
        case EventPhase::INSTANT: ph = 'I'; break;
        case EventPhase::ALLOC:   ph = 'I'; break;
        case EventPhase::FREE:    ph = 'I'; break;
        default:                  ph = 'I'; break;
    }
    uint64_t ts_us = e.timestamp_ns / 1000;
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"span","ph":"%c","cat":"%s","name":"%s","ts":%llu,"pid":0,"tid":%u})",
        ph,
        e.category ? e.category : "",
        e.name     ? e.name     : "",
        (unsigned long long)ts_us,
        e.thread_id);
    return buf;
}

std::string ProfilerNode::serializeMemSnapshot(uint64_t ts_ns) const {
    auto snap = MemoryTracker::snapshot();
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"mem","ts":%llu,"live":%zu,"bytes":%zu,"peak":%zu})",
        (unsigned long long)ts_ns,
        snap.live_count, snap.live_bytes, snap.peak_bytes);
    return buf;
}

} // namespace snf::profiler

// ── Auto-init / shutdown wired to Application ────────────────────────────────
namespace snf::profiler::detail {

static ProfilerNode*   g_profilerNode   = nullptr;
static ProfilerServer* g_profilerServer = nullptr;
static SysMonitor*     g_sysMonitor     = nullptr;

namespace {
uint64_t nowNs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

uint32_t currentThreadId()
{
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}
}  // namespace

void init() {
    if (g_profilerNode) return;

    MemoryTracker::enable();

    g_profilerNode   = new ProfilerNode();
    g_sysMonitor     = new SysMonitor();
    g_profilerServer = new ProfilerServer(g_profilerNode, g_sysMonitor);
}

void shutdown() {
    // Explicitly delete in reverse dependency order so signal connections
    // from ProfilerServer into ProfilerNode/SysMonitor are torn down first.
    // This must be called after the ThreadPool is stopped (workers joined)
    // but before m_eventLoops.clear(), so each node can still unregister
    // from its EventLoop in Node::~Node().
    // NOTE: MemoryTracker::disable() is NOT called here — it must happen
    // only after all EventLoop I/O threads have been joined (see disableMemoryTracker()).
    delete g_profilerServer;  g_profilerServer = nullptr;
    delete g_sysMonitor;      g_sysMonitor     = nullptr;
    delete g_profilerNode;    g_profilerNode   = nullptr;
}

void disableMemoryTracker() {
    // Called by Application after m_eventLoops.clear() — all I/O threads are
    // guaranteed to have been joined by this point, so no thread can be inside
    // recordAlloc/recordFree while we destroy the MemoryTracker instance.
    MemoryTracker::disable();
}

ProfilerNode*   profilerNode()   { return g_profilerNode; }
ProfilerServer* profilerServer() { return g_profilerServer; }
SysMonitor*     sysMonitor()     { return g_sysMonitor; }

void onEventLoopProcessingBegin()
{
    TraceEvent e{};
    e.timestamp_ns  = nowNs();
    e.thread_id     = currentThreadId();
    e.phase         = EventPhase::BEGIN;
    e.category      = "eventloop";
    e.name          = "processing";
    e.payload_bytes = 0;
    TraceBuffer::current().push(e);
}

void onEventLoopProcessingEnd()
{
    TraceEvent e{};
    e.timestamp_ns  = nowNs();
    e.thread_id     = currentThreadId();
    e.phase         = EventPhase::END;
    e.category      = "eventloop";
    e.name          = "";
    e.payload_bytes = 0;
    TraceBuffer::current().push(e);
}

} // namespace snf::profiler::detail
