cmake_minimum_required(VERSION 3.26)
project(SNFCore)

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
)

target_include_directories(SNFCore
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:include>
)

install(TARGETS SNFCore
    ARCHIVE DESTINATION lib
    FILE_SET HEADERS DESTINATION include
)

if (ENABLE_TEST)
    add_subdirectory(test)
endif()
