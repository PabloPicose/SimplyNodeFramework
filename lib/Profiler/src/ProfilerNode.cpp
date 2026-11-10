#include "Profiler/ProfilerNode.h"
#include "Profiler/ProfilerServer.h"
#include "Profiler/Serialization.h"
#include "Profiler/SysMonitor.h"
#include "Profiler/TraceBuffer.h"
#include "SNFCore/Timer.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace snf::profiler {

ProfilerNode::ProfilerNode(snf::Node* parent)
    : snf::Node(parent)
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    uint64_t ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());

    // Resolve the directory containing this executable and place output
    // files in a "profiler/" subdirectory next to the binary.
    char exePath[1024] = {};
    ssize_t len = ::readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    std::string dir = (len > 0) ? std::string(exePath, len) : ".";
    auto sep = dir.rfind('/');
    if (sep != std::string::npos) dir.resize(sep);

    std::string outDir = dir + "/profiler";
    ::mkdir(outDir.c_str(), 0755);  // no-op if already exists

    char filename[64];
    std::snprintf(filename, sizeof(filename), "snf_profile_%llu.json", (unsigned long long)ts);
    m_traceFilePath = outDir + "/" + filename;
    m_traceFile.open(m_traceFilePath, std::ios::app);
    m_fileOpen = m_traceFile.is_open();

    auto* timer = new snf::Timer(this);
    timer->timeout.connect([this]() { drainAndProcess(); });
    timer->start(std::chrono::milliseconds(PROFILER_FLUSH_INTERVAL_MS));
    moveToThreadPool();
}

ProfilerNode::~ProfilerNode() {
    if (m_traceFile.is_open())
        m_traceFile.close();
}

void ProfilerNode::drainAndProcess() {
    // One message per batch, not per event: each message is a task on the
    // server's EventLoop and a WebSocket frame, and every loop wake-up is
    // itself traced.
    m_events.clear();
    TraceBuffer::drainAll(m_events);
    // The first message of the tick carries the losses; one is sent even
    // without events so that a full buffer never goes unnoticed.
    uint64_t dropped = TraceBuffer::takeDroppedAll();

    size_t first = 0;
    do {
        const size_t count = std::min<size_t>(PROFILER_CHUNK_SIZE, m_events.size() - first);
        if (count == 0 && dropped == 0) break;
        if (m_fileOpen) {
            for (size_t i = first; i < first + count; ++i)
                m_traceFile << detail::serializeEvent(m_events[i]) << '\n';
        }
        broadcastMessage.emit(detail::serializeEventBatch(m_events.data() + first, count, dropped));
        dropped = 0;
        first += count;
    } while (first < m_events.size());

    // Sent on every tick, even without events, so the memory graph keeps moving.
    const uint64_t ts_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    std::string memJson = detail::serializeMemSnapshot(MemoryTracker::snapshot(), ts_ns);
    if (m_fileOpen)
        m_traceFile << memJson << '\n';
    broadcastMessage.emit(memJson);
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
    // Safe while other threads allocate: disabling only clears the session id.
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
