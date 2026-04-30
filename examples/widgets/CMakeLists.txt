cmake_minimum_required(VERSION 3.26)

add_executable(snf_widgets_table_model_example
    table_model_example.cpp
)

target_link_libraries(snf_widgets_table_model_example PRIVATE SNFWidgets)

target_compile_options(snf_widgets_table_model_example PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_widgets_table_model_example PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_widgets_table_model_example PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_widgets_table_model_example"
    )
endif()
