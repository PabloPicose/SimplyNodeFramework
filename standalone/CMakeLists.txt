cmake_minimum_required(VERSION 3.26)

project(snf_web_standalone_demo
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── SimplyNodeFramework ───────────────────────────────────────────────────────
#
# When this example lives inside a SimplyNodeFramework checkout, prefer the local
# sources so fixes in lib/HttpServer are used immediately. If copied elsewhere,
# it falls back to FetchContent and remains self-contained.
include(FetchContent)

set(SNF_STANDALONE_LOCAL_SNF_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.."
    CACHE PATH "Local SimplyNodeFramework checkout to use before FetchContent"
)

option(SNF_STANDALONE_USE_LOCAL_SNF
    "Prefer SNF_STANDALONE_LOCAL_SNF_DIR before downloading SimplyNodeFramework"
    ON
)

set(SNF_STANDALONE_SNF_GIT_REPOSITORY
    "https://github.com/PabloPicose/SimplyNodeFramework.git"
    CACHE STRING "SimplyNodeFramework Git repository"
)
set(SNF_STANDALONE_SNF_GIT_TAG
    "v0.1"
    CACHE STRING "SimplyNodeFramework Git tag/branch/commit"
)

set(SNF_ENABLE_TESTS OFF CACHE BOOL "Disable SNF tests in standalone demo")
set(SNF_WEB_ASSEMBLY OFF CACHE BOOL "Build SNF for WebAssembly")

set(SNF_STANDALONE_USING_LOCAL_SNF OFF)
if(SNF_STANDALONE_USE_LOCAL_SNF
        AND EXISTS "${SNF_STANDALONE_LOCAL_SNF_DIR}/CMakeLists.txt"
        AND EXISTS "${SNF_STANDALONE_LOCAL_SNF_DIR}/lib/Core/CMakeLists.txt"
        AND EXISTS "${SNF_STANDALONE_LOCAL_SNF_DIR}/lib/HttpServer/CMakeLists.txt"
)
    get_filename_component(SNF_STANDALONE_LOCAL_SNF_DIR
        "${SNF_STANDALONE_LOCAL_SNF_DIR}" ABSOLUTE
    )
    message(STATUS "SNF standalone: using local SimplyNodeFramework at ${SNF_STANDALONE_LOCAL_SNF_DIR}")
    add_subdirectory(
        "${SNF_STANDALONE_LOCAL_SNF_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/simplynodeframework-local-build"
        EXCLUDE_FROM_ALL
    )
    set(SNF_STANDALONE_SNF_SOURCE_DIR "${SNF_STANDALONE_LOCAL_SNF_DIR}")
    set(SNF_STANDALONE_USING_LOCAL_SNF ON)
endif()

if(NOT SNF_STANDALONE_USING_LOCAL_SNF)
    FetchContent_Declare(
        SimplyNodeFramework
        GIT_REPOSITORY "${SNF_STANDALONE_SNF_GIT_REPOSITORY}"
        GIT_TAG        "${SNF_STANDALONE_SNF_GIT_TAG}"
        GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(SimplyNodeFramework)
    FetchContent_GetProperties(SimplyNodeFramework SOURCE_DIR SNF_STANDALONE_SNF_SOURCE_DIR)
endif()

if(NOT SNF_STANDALONE_SNF_SOURCE_DIR)
    message(FATAL_ERROR
        "SNF standalone: could not resolve SimplyNodeFramework source directory from FetchContent."
    )
endif()

set(SNF_STANDALONE_EMSCRIPTEN_SHELL_FILE
    "${SNF_STANDALONE_SNF_SOURCE_DIR}/lib/Widgets/shell.html"
)

if(NOT EXISTS "${SNF_STANDALONE_EMSCRIPTEN_SHELL_FILE}")
    message(FATAL_ERROR
        "SNF standalone: shell template not found at ${SNF_STANDALONE_EMSCRIPTEN_SHELL_FILE}. "
        "Add it to the SNF repo under lib/Widgets/shell.html."
    )
endif()

# ifdef SNF_WEB_ASSEMBLY include subdir app/webclient
add_subdirectory(app/webclient)
if (NOT SNF_WEB_ASSEMBLY)
    add_subdirectory(app/http)
endif()
