#pragma once

#ifdef SNF_ENABLE_PROFILING
#  include "TraceBuffer.h"
#  include "TraceEvent.h"
#  include <chrono>
#  include <cstdint>
#  include <functional>
#  include <thread>

namespace snf::profiler::detail {
    /// Returns a monotonic timestamp in nanoseconds.
    inline uint64_t now_ns() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    /// Returns a hashed thread identifier for trace records.
    inline uint32_t tid() {
        return static_cast<uint32_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
}

/**
 * @brief RAII span guard used by TRACE_EVENT.
 */
namespace snf::profiler {
struct ScopeGuard {
    const char* category;
    ScopeGuard(const char* cat, const char* name, const char* /*file*/, int /*line*/) : category(cat) {
        TraceEvent e{};
        e.timestamp_ns  = detail::now_ns();
        e.thread_id     = detail::tid();
        e.phase         = EventPhase::BEGIN;
        e.category      = cat;
        e.name          = name;
        e.payload_bytes = 0;
        TraceBuffer::current().push(e);
    }
    ~ScopeGuard() {
        TraceEvent e{};
        e.timestamp_ns  = detail::now_ns();
        e.thread_id     = detail::tid();
        e.phase         = EventPhase::END;
        e.category      = category;
        e.name          = "";
        e.payload_bytes = 0;
        TraceBuffer::current().push(e);
    }
};
}

/// Starts a scope trace using the current function name.
#  define TRACE_EVENT_1(_snf_cat_) \
    ::snf::profiler::ScopeGuard _snf_scope_##__LINE__(_snf_cat_, __func__, __FILE__, __LINE__)

/// Starts a scope trace using an explicit span name.
#  define TRACE_EVENT_2(_snf_cat_, _snf_name_) \
    ::snf::profiler::ScopeGuard _snf_scope_##__LINE__(_snf_cat_, _snf_name_, __FILE__, __LINE__)

#  define TRACE_EVENT_GET_MACRO(_1, _2, MACRO, ...) MACRO
#  define TRACE_EVENT(...) TRACE_EVENT_GET_MACRO(__VA_ARGS__, TRACE_EVENT_2, TRACE_EVENT_1)(__VA_ARGS__)

/// Emits a BEGIN trace event.
#  define TRACE_EVENT_BEGIN(_snf_cat_, _snf_name_) \
    do { \
        ::snf::profiler::TraceEvent _e{}; \
        _e.timestamp_ns  = ::snf::profiler::detail::now_ns(); \
        _e.thread_id     = ::snf::profiler::detail::tid(); \
        _e.phase         = ::snf::profiler::EventPhase::BEGIN; \
        _e.category      = (_snf_cat_); \
        _e.name          = (_snf_name_); \
        _e.payload_bytes = 0; \
        ::snf::profiler::TraceBuffer::current().push(_e); \
    } while(0)

/// Emits an END trace event.
#  define TRACE_EVENT_END(_snf_cat_) \
    do { \
        ::snf::profiler::TraceEvent _e{}; \
        _e.timestamp_ns  = ::snf::profiler::detail::now_ns(); \
        _e.thread_id     = ::snf::profiler::detail::tid(); \
        _e.phase         = ::snf::profiler::EventPhase::END; \
        _e.category      = (_snf_cat_); \
        _e.name          = ""; \
        _e.payload_bytes = 0; \
        ::snf::profiler::TraceBuffer::current().push(_e); \
    } while(0)

/// Emits an allocation marker for the memory timeline.
#  define TRACE_MEMORY_ALLOC(_snf_ptr_, _snf_size_) \
    do { \
        ::snf::profiler::TraceEvent _e{}; \
        _e.timestamp_ns  = ::snf::profiler::detail::now_ns(); \
        _e.thread_id     = ::snf::profiler::detail::tid(); \
        _e.phase         = ::snf::profiler::EventPhase::ALLOC; \
        _e.category      = "memory"; \
        _e.name          = "alloc"; \
        _e.payload_bytes = (_snf_size_); \
        ::snf::profiler::TraceBuffer::current().push(_e); \
    } while(0)

/// Emits a free marker for the memory timeline.
#  define TRACE_MEMORY_FREE(_snf_ptr_) \
    do { \
        ::snf::profiler::TraceEvent _e{}; \
        _e.timestamp_ns  = ::snf::profiler::detail::now_ns(); \
        _e.thread_id     = ::snf::profiler::detail::tid(); \
        _e.phase         = ::snf::profiler::EventPhase::FREE; \
        _e.category      = "memory"; \
        _e.name          = "free"; \
        _e.payload_bytes = 0; \
        ::snf::profiler::TraceBuffer::current().push(_e); \
    } while(0)

#else // SNF_ENABLE_PROFILING not defined — all macros are no-ops

/// No-op when profiling is disabled.
#  define TRACE_EVENT(...)         do {} while(0)
/// No-op when profiling is disabled.
#  define TRACE_EVENT_BEGIN(...)   do {} while(0)
/// No-op when profiling is disabled.
#  define TRACE_EVENT_END(...)     do {} while(0)
/// No-op when profiling is disabled.
#  define TRACE_MEMORY_ALLOC(...) do {} while(0)
/// No-op when profiling is disabled.
#  define TRACE_MEMORY_FREE(...)  do {} while(0)

#endif // SNF_ENABLE_PROFILING
