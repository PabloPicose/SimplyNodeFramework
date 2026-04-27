cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFCORE_DIR "${SRC_DIR}/SNFCore")

add_library(SNFCore STATIC)

target_sources(SNFCore
    PRIVATE
        "${SNFCORE_DIR}/Application.cpp"
        "${SNFCORE_DIR}/Node.cpp"
        "${SNFCORE_DIR}/NodePtr.cpp"
        "${SNFCORE_DIR}/Timer.cpp"
        "${SNFCORE_DIR}/EventLoop.cpp"
        "${SNFCORE_DIR}/Connection.cpp"
        "${SNFCORE_DIR}/EpollPoller.cpp"
        "${SNFCORE_DIR}/CommandLineOption.cpp"
        "${SNFCORE_DIR}/CommandLineParser.cpp"
        "${SNFCORE_DIR}/IniParser.cpp"
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
            "${SNFCORE_DIR}/EpollPoller.h"
            "${SNFCORE_DIR}/CommandLineOption.h"
            "${SNFCORE_DIR}/CommandLineParser.h"
            "${SNFCORE_DIR}/IniParser.h"
)

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

if (SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
