cmake_minimum_required(VERSION 3.26)

# ImGui is compiled as a private dependency of SNFWidgets;
# this example only needs to link SNFWidgets.
add_executable(snf_web_imgui_demo
    main.cpp
)

target_link_libraries(snf_web_imgui_demo PRIVATE SNFWidgets)

if(SNF_ENABLE_PROFILING)
    target_link_libraries(snf_web_imgui_demo PRIVATE SNFProfiler)
endif()

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
