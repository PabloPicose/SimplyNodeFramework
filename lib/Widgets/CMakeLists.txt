cmake_minimum_required(VERSION 3.26)

include(FetchContent)

# Reusable Emscripten shell template path for consumers built from source.
set(SNFWIDGETS_EMSCRIPTEN_SHELL_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    CACHE INTERNAL "Path to SNFWidgets Emscripten shell.html template"
)

# ── Dear ImGui (internal, not exposed to consumers) ──────────────────────────
#
# ImGui is fetched and compiled as a PRIVATE dependency of SNFWidgets.
# Its headers are NOT added to SNFWidgets' public include path, so consumers
# do not need (and should not) include imgui.h.  The content name
# `snf_imgui` is intentionally different from the common `imgui` to avoid
# conflicts when a parent project also declares its own ImGui via FetchContent.
FetchContent_Declare(
    snf_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(snf_imgui)
if(NOT snf_imgui_POPULATED)
    FetchContent_Populate(snf_imgui)
endif()
# snf_imgui_SOURCE_DIR is now set.

# ── GLFW (desktop only) ───────────────────────────────────────────────────────
#
# On Emscripten the contrib.glfw3 port is used instead (injected via
# --use-port=contrib.glfw3 compile/link flags; no CMake target is needed).
# On desktop we fetch GLFW as a static library via FetchContent.
if(NOT EMSCRIPTEN)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        snf_glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(snf_glfw)

    find_package(OpenGL REQUIRED)
endif()

# ── Internal ImGui static library ────────────────────────────────────────────
# Not installed, not exported.
add_library(SNFWidgets_imgui STATIC
    ${snf_imgui_SOURCE_DIR}/imgui.cpp
    ${snf_imgui_SOURCE_DIR}/imgui_demo.cpp
    ${snf_imgui_SOURCE_DIR}/imgui_draw.cpp
    ${snf_imgui_SOURCE_DIR}/imgui_tables.cpp
    ${snf_imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${snf_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${snf_imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# PUBLIC so that SNFWidgets' own .cpp files can #include "imgui.h" etc.
target_include_directories(SNFWidgets_imgui PUBLIC
    ${snf_imgui_SOURCE_DIR}
    ${snf_imgui_SOURCE_DIR}/backends
)

target_compile_features(SNFWidgets_imgui PUBLIC cxx_std_17)

# Disable file I/O helpers that are incompatible with -sNO_FILESYSTEM=1.
target_compile_definitions(SNFWidgets_imgui PUBLIC IMGUI_DISABLE_FILE_FUNCTIONS)

if(EMSCRIPTEN)
    # The contrib.glfw3 Emscripten port provides <GLFW/glfw3.h> at compile
    # time; imgui_impl_glfw.cpp needs it.
    target_compile_options(SNFWidgets_imgui PRIVATE --use-port=contrib.glfw3)
else()
    # Desktop: GLFW headers come from the fetched glfw target.
    # OpenGL::GL provides the system libGL.so for linking.
    target_link_libraries(SNFWidgets_imgui PRIVATE glfw OpenGL::GL)
endif()

# ── SNFWidgets library ────────────────────────────────────────────────────────
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFWIDGETS_DIR "${SRC_DIR}/SNFWidgets")

add_library(SNFWidgets STATIC)

target_sources(SNFWidgets
    PRIVATE
        "${SNFWIDGETS_DIR}/ApplicationNode.cpp"
        "${SNFWIDGETS_DIR}/Widget.cpp"
        "${SNFWIDGETS_DIR}/Layout.cpp"
        "${SNFWIDGETS_DIR}/Window.cpp"
        "${SNFWIDGETS_DIR}/MessageBox.cpp"
        "${SNFWIDGETS_DIR}/PushButton.cpp"
        "${SNFWIDGETS_DIR}/ImGuiDemoWindow.cpp"
        "${SNFWIDGETS_DIR}/Label.cpp"
        "${SNFWIDGETS_DIR}/CheckBox.cpp"
        "${SNFWIDGETS_DIR}/RadioButton.cpp"
        "${SNFWIDGETS_DIR}/Slider.cpp"
        "${SNFWIDGETS_DIR}/SpinBox.cpp"
        "${SNFWIDGETS_DIR}/LineEdit.cpp"
        "${SNFWIDGETS_DIR}/TextEdit.cpp"
        "${SNFWIDGETS_DIR}/ComboBox.cpp"
        "${SNFWIDGETS_DIR}/ProgressBar.cpp"
        "${SNFWIDGETS_DIR}/TableView.cpp"
        "${SNFWIDGETS_DIR}/DataWidgetMapper.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            "${SNFWIDGETS_DIR}/ApplicationNode.h"
            "${SNFWIDGETS_DIR}/WebApplicationNode.h"
            "${SNFWIDGETS_DIR}/Widget.h"
            "${SNFWIDGETS_DIR}/Layout.h"
            "${SNFWIDGETS_DIR}/Window.h"
            "${SNFWIDGETS_DIR}/MessageBox.h"
            "${SNFWIDGETS_DIR}/PushButton.h"
            "${SNFWIDGETS_DIR}/ImGuiDemoWindow.h"
            "${SNFWIDGETS_DIR}/Label.h"
            "${SNFWIDGETS_DIR}/CheckBox.h"
            "${SNFWIDGETS_DIR}/RadioButton.h"
            "${SNFWIDGETS_DIR}/Slider.h"
            "${SNFWIDGETS_DIR}/SpinBox.h"
            "${SNFWIDGETS_DIR}/LineEdit.h"
            "${SNFWIDGETS_DIR}/TextEdit.h"
            "${SNFWIDGETS_DIR}/ComboBox.h"
            "${SNFWIDGETS_DIR}/ProgressBar.h"
            "${SNFWIDGETS_DIR}/TableView.h"
            "${SNFWIDGETS_DIR}/DataWidgetMapper.h"
)

target_include_directories(SNFWidgets
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(SNFWidgets
    PUBLIC  SNFCore
    PRIVATE SNFWidgets_imgui   # imgui is implementation-private
)

target_compile_features(SNFWidgets PUBLIC cxx_std_17)

if(EMSCRIPTEN)
    # --use-port=contrib.glfw3 must appear in both compile and link flags:
    #   PRIVATE compile: SNFWidgets' own .cpp files (#include <GLFW/glfw3.h>)
    #   INTERFACE compile + link: propagated to any executable that links SNFWidgets
    target_compile_options(SNFWidgets
        PRIVATE    --use-port=contrib.glfw3
        INTERFACE  --use-port=contrib.glfw3
    )
    target_link_options(SNFWidgets INTERFACE
        --use-port=contrib.glfw3
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sFULL_ES3=1
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
    )
else()
    # Desktop: link GLFW and OpenGL.  PRIVATE keeps them out of the public
    # interface (consumers do not need GLFW headers in their own code).
    target_link_libraries(SNFWidgets PRIVATE glfw OpenGL::GL)
endif()

add_library(SNFWidgets::SNFWidgets ALIAS SNFWidgets)

# ── Examples ─────────────────────────────────────────────────────────────────
# Examples are only built when SimplyNodeFramework is the top-level project.
# When this file is consumed as a subdirectory (e.g. from standalone/ or via
# FetchContent) PROJECT_IS_TOP_LEVEL is FALSE and the examples are skipped,
# so that external consumers do not get unwanted build targets.
#
# Note: lib/Widgets/examples/standalone/ is intentionally NOT added here;
# it is a self-contained project meant to be built independently.
if(NOT DEFINED SNF_ENABLE_EXAMPLES)
    option(SNF_ENABLE_EXAMPLES "Build SimplyNodeFramework examples" ${PROJECT_IS_TOP_LEVEL})
endif()

if(SNF_ENABLE_EXAMPLES AND PROJECT_IS_TOP_LEVEL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
