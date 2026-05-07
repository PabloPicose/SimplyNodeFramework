cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFCORE_DIR "${SRC_DIR}/SNFCore")

add_library(SNFCore STATIC)

find_package(Threads REQUIRED)

# ── Platform-specific I/O poller implementation ───────────────────────────────
# On Linux the real epoll/eventfd backend is used.
# On WebAssembly the stub is compiled instead: same EpollPoller contract,
# no Linux syscalls, wait() returns immediately.
if(SNF_WEB_ASSEMBLY)
    set(SNFCORE_POLLER_SRC "${SNFCORE_DIR}/EpollPollerStub.cpp")
else()
    set(SNFCORE_POLLER_SRC "${SNFCORE_DIR}/EpollPoller.cpp")
endif()

target_sources(SNFCore
    PRIVATE
        "${SNFCORE_DIR}/Application.cpp"
        "${SNFCORE_DIR}/Node.cpp"
        "${SNFCORE_DIR}/NodePtr.cpp"
        "${SNFCORE_DIR}/Timer.cpp"
        "${SNFCORE_DIR}/EventLoop.cpp"
        "${SNFCORE_DIR}/Connection.cpp"
        "${SNFCORE_DIR}/Runnable.cpp"
        "${SNFCORE_DIR}/ThreadPool.cpp"
        "${SNFCORE_DIR}/ModelIndex.cpp"
        "${SNFCORE_DIR}/AbstractTableModel.cpp"
        "${SNFCORE_DIR}/Variant.cpp"
        "${SNFCORE_DIR}/File.cpp"
        "${SNFCORE_DIR}/TempFile.cpp"
        ${SNFCORE_POLLER_SRC}
        "${SNFCORE_DIR}/CommandLineOption.cpp"
        "${SNFCORE_DIR}/CommandLineParser.cpp"
        "${SNFCORE_DIR}/IniParser.cpp"
        "${SNFCORE_DIR}/Logger.cpp"
        "${SNFCORE_DIR}/ConsoleLogSink.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            "${SNFCORE_DIR}/Application.h"
            "${SNFCORE_DIR}/Node.h"
            "${SNFCORE_DIR}/NodePtr.h"
            "${SNFCORE_DIR}/Timer.h"
            "${SNFCORE_DIR}/EventLoop.h"
            "${SNFCORE_DIR}/Connection.h"
            "${SNFCORE_DIR}/Runnable.h"
            "${SNFCORE_DIR}/ThreadPool.h"
            "${SNFCORE_DIR}/ModelIndex.h"
            "${SNFCORE_DIR}/AbstractTableModel.h"
            "${SNFCORE_DIR}/Variant.h"
            "${SNFCORE_DIR}/ByteArray.h"
            "${SNFCORE_DIR}/File.h"
            "${SNFCORE_DIR}/TempFile.h"
            "${SNFCORE_DIR}/EpollPoller.h"
            "${SNFCORE_DIR}/CommandLineOption.h"
            "${SNFCORE_DIR}/CommandLineParser.h"
            "${SNFCORE_DIR}/IniParser.h"
            "${SNFCORE_DIR}/LogLevel.h"
            "${SNFCORE_DIR}/LogMessage.h"
            "${SNFCORE_DIR}/LogSink.h"
            "${SNFCORE_DIR}/Logger.h"
            "${SNFCORE_DIR}/ConsoleLogSink.h"
            "${SNFCORE_DIR}/SignalLogSink.h"
            "${SNFCORE_DIR}/Logging.h"
)

# Expose the web-platform flag only to SNFCore's own translation units.
# Downstream consumers (e.g. SNFWidgets) should use the standard __EMSCRIPTEN__
# macro provided by the toolchain if they need platform-specific branches.
if(SNF_WEB_ASSEMBLY)
    target_compile_definitions(SNFCore PRIVATE SNF_PLATFORM_WEB)
endif()

# If using GCC < 9, we need to link against stdc++fs to get filesystem support.
target_link_libraries(SNFCore
    PUBLIC
        Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" 
   AND 
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)

    target_link_libraries(SNFCore
        PUBLIC
            stdc++fs
    )

endif()

target_include_directories(SNFCore
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(SNFCore PUBLIC cxx_std_17)

add_library(SNFCore::SNFCore ALIAS SNFCore)

install(TARGETS SNFCore
    EXPORT SNFCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFCoreTargets
    FILE SNFCoreTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFCore
)

if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
