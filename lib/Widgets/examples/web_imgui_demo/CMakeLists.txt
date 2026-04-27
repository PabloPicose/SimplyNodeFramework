cmake_minimum_required(VERSION 3.26)

include(FetchContent)

# ── Download ImGui sources ────────────────────────────────────────────────────
# ImGui does not ship a CMakeLists.txt, so we use FetchContent_Populate
# to download only, without adding a subdirectory.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# ── ImGui static library ──────────────────────────────────────────────────────
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_features(imgui PUBLIC cxx_std_17)

if(EMSCRIPTEN)
    # imgui_impl_glfw.cpp includes <GLFW/glfw3.h>.  Adding --use-port here
    # injects the contrib.glfw3 include path for imgui's own compilation
    # (INTERFACE link options on SNFWidgets do not affect static library builds).
    target_compile_options(imgui PRIVATE --use-port=contrib.glfw3)

    # With -sNO_FILESYSTEM=1 on the final binary, ImGui must not attempt any
    # file I/O (e.g. loading fonts from disk).  This define disables those paths.
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_FILE_FUNCTIONS)
endif()

# ── Example executable ────────────────────────────────────────────────────────
add_executable(snf_web_imgui_demo
    main.cpp
)

target_link_libraries(snf_web_imgui_demo PRIVATE
    SNFWidgets
    imgui
)

target_compile_options(snf_web_imgui_demo PRIVATE -Wall -Wextra)

if(EMSCRIPTEN)
    target_link_options(snf_web_imgui_demo PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/../../shell.html
    )

    set_target_properties(snf_web_imgui_demo PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_web_imgui_demo"
    )
endif()
