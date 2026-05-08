#pragma once

#ifdef SNF_ENABLE_PROFILING

/**
 * @brief Public profiler integration hooks.
 *
 * The application constructor calls SNF_PROFILER_INIT automatically when
 * profiling is enabled, and shutdown tears the subsystem down before exit.
 */
// Forward declarations for test-support getters
namespace snf::profiler {
    class ProfilerNode;
    class ProfilerServer;
    class SysMonitor;
}

namespace snf::profiler::detail {
    /// Initializes the profiler subsystem.
    void init();

    /// Shuts down the profiler subsystem.
    void shutdown();
    // Called by Application AFTER m_eventLoops.clear() to disable the memory
    // tracker once all I/O threads have been joined and can no longer call
    // operator new / recordAlloc.
    /// Disables the memory tracker after all event loops have been cleared.
    void disableMemoryTracker();

    // Accessors for the globally auto-initialised instances.
    // Returns nullptr before init() or after shutdown().
    /// Returns the global profiler node, or nullptr when unavailable.
    ProfilerNode*   profilerNode();
    /// Returns the global profiler server, or nullptr when unavailable.
    ProfilerServer* profilerServer();
    /// Returns the global system monitor, or nullptr when unavailable.
    SysMonitor*     sysMonitor();

    /// Emitted at the start of each EventLoop active-processing phase.
    /// The span covers tasks + timers + I/O callbacks + root-node ticks.
    /// The idle (sleeping) portion of the loop is excluded.
    void onEventLoopProcessingBegin();

    /// Emitted at the end of each EventLoop active-processing phase,
    /// just before the loop blocks waiting for more work.
    void onEventLoopProcessingEnd();
}

// SNF_PROFILER_INIT() is called automatically by snf::Application constructor
// (at the end, after ThreadPool is created) when SNF_ENABLE_PROFILING is defined.
// Users should NOT call this manually.
/// Initializes the profiler subsystem.
#  define SNF_PROFILER_INIT()     ::snf::profiler::detail::init()
/// Shuts down the profiler subsystem.
#  define SNF_PROFILER_SHUTDOWN() ::snf::profiler::detail::shutdown()

#else

/// No-op when profiling is disabled.
#  define SNF_PROFILER_INIT()     do {} while(0)
/// No-op when profiling is disabled.
#  define SNF_PROFILER_SHUTDOWN() do {} while(0)

#endif
