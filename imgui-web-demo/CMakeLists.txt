cmake_minimum_required(VERSION 3.20)

project(imgui_web_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

add_executable(imgui_web_demo
    main.cpp

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp

    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_web_demo PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_compile_options(imgui_web_demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Os
)

target_compile_definitions(imgui_web_demo PRIVATE
    IMGUI_DISABLE_FILE_FUNCTIONS
)

if (EMSCRIPTEN)
    target_link_options(imgui_web_demo PRIVATE
        -sWASM=1
        --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html
        -sALLOW_MEMORY_GROWTH=1
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1

        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2

        --use-port=contrib.glfw3
    )

    set_target_properties(imgui_web_demo PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
    )
endif()
