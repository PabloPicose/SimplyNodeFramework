#pragma once

#ifdef SNF_ENABLE_PROFILING

// Forward declarations for test-support getters
namespace snf::profiler {
    class ProfilerNode;
    class ProfilerServer;
    class SysMonitor;
}

namespace snf::profiler::detail {
    void init();
    void shutdown();
    // Called by Application AFTER m_eventLoops.clear() to disable the memory
    // tracker once all I/O threads have been joined and can no longer call
    // operator new / recordAlloc.
    void disableMemoryTracker();

    // Accessors for the globally auto-initialised instances.
    // Returns nullptr before init() or after shutdown().
    ProfilerNode*   profilerNode();
    ProfilerServer* profilerServer();
    SysMonitor*     sysMonitor();
}

// SNF_PROFILER_INIT() is called automatically by snf::Application constructor
// (at the end, after ThreadPool is created) when SNF_ENABLE_PROFILING is defined.
// Users should NOT call this manually.
#  define SNF_PROFILER_INIT()     ::snf::profiler::detail::init()
#  define SNF_PROFILER_SHUTDOWN() ::snf::profiler::detail::shutdown()

#else

#  define SNF_PROFILER_INIT()     do {} while(0)
#  define SNF_PROFILER_SHUTDOWN() do {} while(0)

#endif
