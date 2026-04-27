cmake_minimum_required(VERSION 3.26)

project(snf_web_standalone_demo
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── SimplyNodeFramework ───────────────────────────────────────────────────────
#
# This standalone example intentionally avoids any path reference outside this
# folder tree and does not rely on network fetches by default.
#
# The framework must be vendored under:
#   ./third_party/SimplyNodeFramework
include(FetchContent)

set(SNF_STANDALONE_VENDOR_DIR
    "${CMAKE_CURRENT_LIST_DIR}/third_party/SimplyNodeFramework"
)

if(EXISTS "${SNF_STANDALONE_VENDOR_DIR}/CMakeLists.txt")
    message(STATUS "SNF standalone: using vendored SimplyNodeFramework from ${SNF_STANDALONE_VENDOR_DIR}")
    FetchContent_Declare(
        SimplyNodeFramework
        SOURCE_DIR "${SNF_STANDALONE_VENDOR_DIR}"
    )
else()
    message(FATAL_ERROR
        "SNF standalone: missing vendored dependency at ${SNF_STANDALONE_VENDOR_DIR}. "
        "Place a SimplyNodeFramework checkout there (with lib/Widgets available)."
    )
endif()

set(SNF_WEB_ASSEMBLY ON  CACHE BOOL "" FORCE)
set(SNF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SimplyNodeFramework)

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
        "Vendor a revision that contains lib/Widgets under third_party/SimplyNodeFramework."
    )
endif()

target_compile_options(snf_standalone_app PRIVATE -Wall -Wextra)

if(EMSCRIPTEN)
    target_link_options(snf_standalone_app PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html
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
# Vendor SimplyNodeFramework in this folder tree:
#
#   mkdir -p third_party
#   git clone https://github.com/PabloPicose/SimplyNodeFramework.git \
#             third_party/SimplyNodeFramework
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
