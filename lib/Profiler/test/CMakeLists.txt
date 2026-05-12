add_executable(SNFProfilerTests
    asan_options.cpp
    TraceBufferTests.cpp
    MemoryTrackerTests.cpp
    MacrosTests.cpp
    ProfilerNodeTests.cpp
    ProfilerServerTests.cpp
    SysMonitorTests.cpp
    EventLoopProfilingTests.cpp
)

target_link_libraries(SNFProfilerTests
    PRIVATE
        SNFProfiler
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(SNFProfilerTests
    TEST_PREFIX "SNFProfilerTests."
    PROPERTIES RESOURCE_LOCK "snf_profiler_global_state"
)
