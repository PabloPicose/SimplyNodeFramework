#include "Profiler/MemoryTracker.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace snf::profiler {

// Global flag (no heap needed)
static std::atomic<bool> g_enabled{false};

// Storage for MemoryTracker allocated with malloc (not operator new)
static std::aligned_storage_t<sizeof(MemoryTracker), alignof(MemoryTracker)> g_storage;
static MemoryTracker* g_instance = nullptr;

// Shared re-entrance guard: prevents both recordAlloc AND recordFree from
// recursing into each other while a lock is already held on this thread.
// A single flag is necessary because m_map.emplace() can internally call
// operator delete (during bucket rehash), which would otherwise deadlock.
static thread_local bool g_inTracker = false;

void MemoryTracker::enable() {
    if (g_enabled.load(std::memory_order_acquire)) return;
    g_instance = new (&g_storage) MemoryTracker();
    g_enabled.store(true, std::memory_order_release);
}

void MemoryTracker::disable() {
    g_enabled.store(false, std::memory_order_release);
    if (g_instance) {
        g_instance->~MemoryTracker();
        g_instance = nullptr;
    }
}

bool MemoryTracker::isEnabled() {
    return g_enabled.load(std::memory_order_acquire);
}

void MemoryTracker::recordAlloc(void* ptr, size_t size) {
    if (!g_enabled.load(std::memory_order_acquire) || !g_instance || !ptr) return;
    if (g_inTracker) return;
    g_inTracker = true;
    {
        std::lock_guard<std::mutex> lk(g_instance->m_mutex);
        auto [it, inserted] = g_instance->m_map.emplace(ptr, size);
        if (inserted) {
            g_instance->m_live_bytes += size;
            ++g_instance->m_live_count;
            if (g_instance->m_live_bytes > g_instance->m_peak_bytes)
                g_instance->m_peak_bytes = g_instance->m_live_bytes;
        }
    }
    g_inTracker = false;
}

void MemoryTracker::recordFree(void* ptr) {
    if (!g_enabled.load(std::memory_order_acquire) || !g_instance || !ptr) return;
    if (g_inTracker) return;
    g_inTracker = true;
    {
        std::lock_guard<std::mutex> lk(g_instance->m_mutex);
        auto it = g_instance->m_map.find(ptr);
        if (it != g_instance->m_map.end()) {
            g_instance->m_live_bytes -= it->second;
            --g_instance->m_live_count;
            g_instance->m_map.erase(it);
        }
    }
    g_inTracker = false;
}

MemSnapshot MemoryTracker::snapshot() {
    if (!g_instance) return {};
    std::lock_guard<std::mutex> lk(g_instance->m_mutex);
    return {g_instance->m_live_count, g_instance->m_live_bytes, g_instance->m_peak_bytes};
}

} // namespace snf::profiler

// Global operator new/delete overrides (only active when profiling is compiled in).
// All throwing and nothrow variants must be overridden together so that ASan
// consistently sees every allocation go through std::malloc.  If only the
// throwing variants are overridden, third-party code (e.g. Mesa/libLLVM) that
// allocates via new(nothrow) and frees via operator delete will trigger an
// ASan alloc-dealloc-mismatch because ASan assigns a different "alloc type"
// to nothrow-new allocations that bypassed our override.
void* operator new(size_t size) {
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    snf::profiler::MemoryTracker::recordAlloc(ptr, size);
    return ptr;
}

void* operator new[](size_t size) {
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    snf::profiler::MemoryTracker::recordAlloc(ptr, size);
    return ptr;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    void* ptr = std::malloc(size);
    if (ptr) snf::profiler::MemoryTracker::recordAlloc(ptr, size);
    return ptr;
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    void* ptr = std::malloc(size);
    if (ptr) snf::profiler::MemoryTracker::recordAlloc(ptr, size);
    return ptr;
}

void operator delete(void* ptr) noexcept {
    snf::profiler::MemoryTracker::recordFree(ptr);
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    snf::profiler::MemoryTracker::recordFree(ptr);
    std::free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    snf::profiler::MemoryTracker::recordFree(ptr);
    std::free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    snf::profiler::MemoryTracker::recordFree(ptr);
    std::free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    snf::profiler::MemoryTracker::recordFree(ptr);
    std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    snf::profiler::MemoryTracker::recordFree(ptr);
    std::free(ptr);
}
