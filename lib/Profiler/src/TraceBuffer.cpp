#include "Profiler/TraceBuffer.h"
#include <algorithm>
#include <mutex>
#include <vector>

namespace snf::profiler {

namespace {
    std::mutex               g_registryMutex;
    std::vector<TraceBuffer*> g_registry;

    // Events and drop counts left by buffers destroyed before the next drain
    // (a thread that exits takes its thread_local buffer with it).
    // Guarded by g_registryMutex.
    std::vector<TraceEvent> g_orphanEvents;
    uint64_t                g_orphanDropped = 0;

    // Bound for g_orphanEvents when nobody drains (profiler not running).
    constexpr size_t kMaxOrphanEvents = TraceBuffer::kCapacity;
} // anonymous namespace

TraceBuffer::TraceBuffer() {
    std::lock_guard<std::mutex> lk(g_registryMutex);
    g_registry.push_back(this);
}

TraceBuffer::~TraceBuffer() {
    // Under the registry lock, so no drainAll() is reading this buffer.
    std::lock_guard<std::mutex> lk(g_registryMutex);
    g_registry.erase(std::remove(g_registry.begin(), g_registry.end(), this), g_registry.end());

    std::vector<TraceEvent> pending;
    drain(pending);
    const size_t room = kMaxOrphanEvents - std::min(kMaxOrphanEvents, g_orphanEvents.size());
    const size_t kept = std::min(room, pending.size());
    g_orphanEvents.insert(g_orphanEvents.end(), pending.begin(), pending.begin() + kept);
    g_orphanDropped += (pending.size() - kept) + takeDropped();
}

bool TraceBuffer::push(const TraceEvent& e) noexcept {
    uint32_t head = m_head.load(std::memory_order_relaxed);
    uint32_t next = (head + 1) % kCapacity;
    if (next == m_tail.load(std::memory_order_acquire)) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;  // full
    }
    m_data[head] = e;
    m_head.store(next, std::memory_order_release);
    return true;
}

void TraceBuffer::drain(std::vector<TraceEvent>& out) {
    uint32_t tail = m_tail.load(std::memory_order_relaxed);
    uint32_t head = m_head.load(std::memory_order_acquire);
    while (tail != head) {
        out.push_back(m_data[tail]);
        tail = (tail + 1) % kCapacity;
    }
    m_tail.store(tail, std::memory_order_release);
}

TraceBuffer& TraceBuffer::current() {
    thread_local TraceBuffer buf;
    return buf;
}

void TraceBuffer::drainAll(std::vector<TraceEvent>& out) {
    std::lock_guard<std::mutex> lk(g_registryMutex);
    // Orphans first: their buffers are gone, so they are older than anything
    // still queued in a live buffer of a different thread.
    out.insert(out.end(), g_orphanEvents.begin(), g_orphanEvents.end());
    g_orphanEvents.clear();
    for (auto* buf : g_registry) buf->drain(out);
}

uint64_t TraceBuffer::takeDropped() noexcept {
    return m_dropped.exchange(0, std::memory_order_relaxed);
}

uint64_t TraceBuffer::takeDroppedAll() {
    std::lock_guard<std::mutex> lk(g_registryMutex);
    uint64_t total = g_orphanDropped;
    g_orphanDropped = 0;
    for (auto* buf : g_registry) total += buf->takeDropped();
    return total;
}

} // namespace snf::profiler
