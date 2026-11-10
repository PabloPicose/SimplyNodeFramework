#include "Profiler/MemoryTracker.h"
#include <atomic>
#include <cstdlib>
#include <new>

namespace snf::profiler {

namespace {

// Prepended to every block returned by operator new. 16 bytes keeps the
// user pointer at the alignment malloc guarantees (__STDCPP_DEFAULT_NEW_ALIGNMENT__).
struct alignas(16) BlockHeader {
    size_t   size;
    uint32_t session;  // 0: allocated while tracking was disabled
    uint32_t reserved;
};
static_assert(sizeof(BlockHeader) == 16, "header must preserve new alignment");

// Current session id, 0 while disabled. A new id per enable() means blocks
// from an earlier session are ignored when freed instead of underflowing
// the counters of the current one.
std::atomic<uint32_t> g_session{0};
std::atomic<uint32_t> g_lastSession{0};

// Counters are spread over cache-line sized slots, one per thread (shared
// round-robin past kSlots threads): a single set of global atomics made 8
// threads doing new/delete about 90x slower than without tracking. Values
// are signed because a block may be freed by a thread other than the one
// that allocated it; only the sum over all slots is meaningful.
constexpr size_t kSlots = 256;
// Peak is refreshed whenever a slot has grown this much since its last sync.
constexpr int64_t kPeakSyncBytes = 64 * 1024;

struct alignas(64) Slot {
    std::atomic<int64_t> count{0};
    std::atomic<int64_t> bytes{0};
    std::atomic<int64_t> syncedBytes{0};
};
Slot g_slots[kSlots];
std::atomic<uint32_t> g_nextSlot{0};
std::atomic<size_t> g_peakBytes{0};

// Trivially destructible on purpose: no TLS destructor to register from
// inside operator new.
thread_local int t_slot = -1;

Slot& mySlot() {
    if (t_slot < 0)
        t_slot = static_cast<int>(g_nextSlot.fetch_add(1, std::memory_order_relaxed) % kSlots);
    return g_slots[t_slot];
}

void totals(int64_t& count, int64_t& bytes) {
    count = 0;
    bytes = 0;
    for (const Slot& s : g_slots) {
        count += s.count.load(std::memory_order_relaxed);
        bytes += s.bytes.load(std::memory_order_relaxed);
    }
}

void raisePeak(size_t live) {
    size_t peak = g_peakBytes.load(std::memory_order_relaxed);
    while (live > peak &&
           !g_peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
    }
}

void accountAlloc(size_t size) {
    Slot& s = mySlot();
    s.count.fetch_add(1, std::memory_order_relaxed);
    const int64_t bytes = s.bytes.fetch_add(static_cast<int64_t>(size), std::memory_order_relaxed)
                        + static_cast<int64_t>(size);
    if (bytes - s.syncedBytes.load(std::memory_order_relaxed) >= kPeakSyncBytes) {
        s.syncedBytes.store(bytes, std::memory_order_relaxed);
        int64_t count, total;
        totals(count, total);
        if (total > 0) raisePeak(static_cast<size_t>(total));
    }
}

void accountFree(size_t size) {
    Slot& s = mySlot();
    s.count.fetch_sub(1, std::memory_order_relaxed);
    const int64_t bytes = s.bytes.fetch_sub(static_cast<int64_t>(size), std::memory_order_relaxed)
                        - static_cast<int64_t>(size);
    // Keep the sync point from drifting above the slot after frees.
    if (bytes < s.syncedBytes.load(std::memory_order_relaxed))
        s.syncedBytes.store(bytes, std::memory_order_relaxed);
}

} // namespace

void MemoryTracker::enable() {
    if (g_session.load(std::memory_order_acquire) != 0) return;
    for (Slot& slot : g_slots) {
        slot.count.store(0, std::memory_order_relaxed);
        slot.bytes.store(0, std::memory_order_relaxed);
        slot.syncedBytes.store(0, std::memory_order_relaxed);
    }
    g_peakBytes.store(0, std::memory_order_relaxed);
    uint32_t id = g_lastSession.fetch_add(1, std::memory_order_relaxed) + 1;
    if (id == 0) id = g_lastSession.fetch_add(1, std::memory_order_relaxed) + 1;  // wrapped
    g_session.store(id, std::memory_order_release);
}

void MemoryTracker::disable() {
    g_session.store(0, std::memory_order_release);
}

bool MemoryTracker::isEnabled() {
    return g_session.load(std::memory_order_acquire) != 0;
}

MemSnapshot MemoryTracker::snapshot() {
    if (!isEnabled()) return {};
    int64_t count, bytes;
    totals(count, bytes);
    // Slots are read one by one while other threads allocate; a transient
    // negative sum is clamped.
    const size_t live = bytes > 0 ? static_cast<size_t>(bytes) : 0;
    raisePeak(live);
    return {count > 0 ? static_cast<size_t>(count) : 0, live,
            g_peakBytes.load(std::memory_order_relaxed)};
}

namespace detail {

void* trackedAlloc(size_t size) noexcept {
    if (size > static_cast<size_t>(-1) - sizeof(BlockHeader)) return nullptr;
    auto* header = static_cast<BlockHeader*>(std::malloc(sizeof(BlockHeader) + size));
    if (!header) return nullptr;
    header->size     = size;
    header->session  = g_session.load(std::memory_order_acquire);
    header->reserved = 0;
    if (header->session != 0) accountAlloc(size);
    return header + 1;
}

void trackedFree(void* ptr) noexcept {
    if (!ptr) return;
    auto* header = static_cast<BlockHeader*>(ptr) - 1;
    if (header->session != 0 &&
        header->session == g_session.load(std::memory_order_acquire))
        accountFree(header->size);
    std::free(header);
}

} // namespace detail

} // namespace snf::profiler

// Global operator new/delete replacements (only linked when profiling is
// compiled in). Every throwing, nothrow and sized variant goes through
// trackedAlloc/trackedFree so that each block has a header; mixing in the
// library's own versions would free a pointer malloc never returned.
// Aligned (std::align_val_t) variants are not replaced and keep the library's
// matching pair, so they are not tracked.
using snf::profiler::detail::trackedAlloc;
using snf::profiler::detail::trackedFree;

void* operator new(size_t size) {
    void* ptr = trackedAlloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](size_t size) {
    void* ptr = trackedAlloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return trackedAlloc(size);
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return trackedAlloc(size);
}

void operator delete(void* ptr) noexcept { trackedFree(ptr); }
void operator delete[](void* ptr) noexcept { trackedFree(ptr); }
void operator delete(void* ptr, size_t) noexcept { trackedFree(ptr); }
void operator delete[](void* ptr, size_t) noexcept { trackedFree(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { trackedFree(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { trackedFree(ptr); }
