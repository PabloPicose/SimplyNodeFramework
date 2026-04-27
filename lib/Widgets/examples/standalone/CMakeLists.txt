cmake_minimum_required(VERSION 3.26)

project(snf_web_standalone_demo
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── SimplyNodeFramework ───────────────────────────────────────────────────────
#
# This standalone example resolves SimplyNodeFramework via FetchContent.
# No manual dependency folder is required.
include(FetchContent)

set(SNF_STANDALONE_SNF_GIT_REPOSITORY
    "https://github.com/PabloPicose/SimplyNodeFramework.git"
    CACHE STRING "SimplyNodeFramework Git repository"
)
set(SNF_STANDALONE_SNF_GIT_TAG
    "main"
    CACHE STRING "SimplyNodeFramework Git tag/branch/commit"
)

FetchContent_Declare(
    SimplyNodeFramework
    GIT_REPOSITORY "${SNF_STANDALONE_SNF_GIT_REPOSITORY}"
    GIT_TAG        "${SNF_STANDALONE_SNF_GIT_TAG}"
    GIT_SHALLOW    TRUE
)

set(SNF_WEB_ASSEMBLY ON  CACHE BOOL "" FORCE)
set(SNF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SimplyNodeFramework)
FetchContent_GetProperties(SimplyNodeFramework SOURCE_DIR SNF_STANDALONE_SNF_SOURCE_DIR)

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

# ── Application executable ────────────────────────────────────────────────────
add_executable(snf_standalone_app main.cpp)

# SNFWidgets::SNFWidgets transitively provides SNFCore, ImGui, and the
# Emscripten GLFW3 port link/compile options.
if(TARGET SNFWidgets::SNFWidgets)
    target_link_libraries(snf_standalone_app PRIVATE SNFWidgets::SNFWidgets)
elseif(TARGET SNFWidgets)
    target_link_libraries(snf_standalone_app PRIVATE SNFWidgets)
else()
    message(FATAL_ERROR
        "SNF standalone: target SNFWidgets was not found in the resolved SimplyNodeFramework source. "
        "Use a branch/tag/commit that contains lib/Widgets."
    )
endif()

target_compile_options(snf_standalone_app PRIVATE -Wall -Wextra)

if(EMSCRIPTEN)
    target_link_options(snf_standalone_app PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNF_STANDALONE_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_standalone_app PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
    )
endif()

# ── Build instructions ────────────────────────────────────────────────────────
#
# Make sure the Emscripten SDK is active before configuring:
#
#   source /path/to/emsdk/emsdk_env.sh
#
# Then configure and build from the repo root (or any working directory):
#
#   emcmake cmake -S . -B build -G Ninja
#   cmake --build build
#
# Serve the result with a local HTTP server:
#
#   cd build/web
#   python3 -m http.server 8000
#   # → http://localhost:8000
