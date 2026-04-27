cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFWIDGETS_DIR "${SRC_DIR}/SNFWidgets")

add_library(SNFWidgets STATIC)

target_sources(SNFWidgets
    PRIVATE
        "${SNFWIDGETS_DIR}/WebApplicationNode.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            "${SNFWIDGETS_DIR}/WebApplicationNode.h"
)

target_include_directories(SNFWidgets
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# SNFWidgets depends on SNFCore for Node, Signal, EventLoop, and Application.
target_link_libraries(SNFWidgets PUBLIC SNFCore)

target_compile_features(SNFWidgets PUBLIC cxx_std_17)

# Emscripten GLFW3 port integration.
#
# The contrib.glfw3 port provides both the GLFW implementation and its public
# headers (<GLFW/glfw3.h>).  We need the port flag in two places:
#   PRIVATE compile: so SNFWidgets' own sources (#include <GLFW/glfw3.h>) compile.
#   INTERFACE compile + link: so any target that links SNFWidgets automatically
#     gets the GLFW include path and the port linked into the final .wasm.
if(EMSCRIPTEN)
    target_compile_options(SNFWidgets
        PRIVATE    --use-port=contrib.glfw3
        INTERFACE  --use-port=contrib.glfw3
    )
    target_link_options(SNFWidgets INTERFACE
        --use-port=contrib.glfw3
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
    )
endif()

add_library(SNFWidgets::SNFWidgets ALIAS SNFWidgets)

# ── Examples ─────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()
