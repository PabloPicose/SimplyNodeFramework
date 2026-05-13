#include "Profiler/TraceBuffer.h"
#include <algorithm>
#include <mutex>
#include <vector>

namespace snf::profiler {

namespace {
    std::mutex               g_registryMutex;
    std::vector<TraceBuffer*> g_registry;
} // anonymous namespace

TraceBuffer::TraceBuffer() {
    std::lock_guard<std::mutex> lk(g_registryMutex);
    g_registry.push_back(this);
}

TraceBuffer::~TraceBuffer() {
    std::lock_guard<std::mutex> lk(g_registryMutex);
    g_registry.erase(std::remove(g_registry.begin(), g_registry.end(), this), g_registry.end());
}

bool TraceBuffer::push(const TraceEvent& e) noexcept {
    uint32_t head = m_head.load(std::memory_order_relaxed);
    uint32_t next = (head + 1) % kCapacity;
    if (next == m_tail.load(std::memory_order_acquire)) return false; // full
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
    for (auto* buf : g_registry) buf->drain(out);
}

} // namespace snf::profiler
