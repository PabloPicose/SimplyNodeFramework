cmake_minimum_required(VERSION 3.26)

set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(SNFProfiler STATIC)

target_sources(SNFProfiler
    PRIVATE
        "${SRC_DIR}/TraceBuffer.cpp"
        "${SRC_DIR}/MemoryTracker.cpp"
        "${SRC_DIR}/ProfilerNode.cpp"
        "${SRC_DIR}/ProfilerServer.cpp"
        "${SRC_DIR}/SysMonitor.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${INCLUDE_DIR}"
        FILES
            "${INCLUDE_DIR}/Profiler/TraceEvent.h"
            "${INCLUDE_DIR}/Profiler/TraceBuffer.h"
            "${INCLUDE_DIR}/Profiler/MemoryTracker.h"
            "${INCLUDE_DIR}/Profiler/ProfilerNode.h"
            "${INCLUDE_DIR}/Profiler/ProfilerServer.h"
            "${INCLUDE_DIR}/Profiler/SysMonitor.h"
            "${INCLUDE_DIR}/Profiler/Macros.h"
            "${INCLUDE_DIR}/Profiler/integration.h"
)

target_link_libraries(SNFProfiler
    PUBLIC
        SNFCore
        SNFWebSocket
)

target_include_directories(SNFProfiler
    PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(SNFProfiler PUBLIC cxx_std_17)

target_compile_definitions(SNFProfiler PUBLIC SNF_ENABLE_PROFILING)

add_library(SNFProfiler::SNFProfiler ALIAS SNFProfiler)

install(TARGETS SNFProfiler
    EXPORT SNFProfilerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFProfilerTargets
    FILE SNFProfilerTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFProfiler
)

if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
