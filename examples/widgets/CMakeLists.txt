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

add_executable(snf_widgets_splitter_example
    splitter_example.cpp
)

target_link_libraries(snf_widgets_splitter_example PRIVATE SNFWidgets)

target_compile_options(snf_widgets_splitter_example PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_widgets_splitter_example PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_widgets_splitter_example PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_widgets_splitter_example"
    )
endif()

add_executable(snf_widgets_scroll_area_example
    scroll_area_example.cpp
)

target_link_libraries(snf_widgets_scroll_area_example PRIVATE SNFWidgets)

target_compile_options(snf_widgets_scroll_area_example PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_widgets_scroll_area_example PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_widgets_scroll_area_example PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_widgets_scroll_area_example"
    )
endif()

add_executable(snf_widgets_tabs_example
    tabs_example.cpp
)

target_link_libraries(snf_widgets_tabs_example PRIVATE SNFWidgets)

target_compile_options(snf_widgets_tabs_example PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_widgets_tabs_example PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_widgets_tabs_example PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_widgets_tabs_example"
    )
endif()

add_executable(snf_widgets_stacked_widget_example
    stacked_widget_example.cpp
)

target_link_libraries(snf_widgets_stacked_widget_example PRIVATE SNFWidgets)

target_compile_options(snf_widgets_stacked_widget_example PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_widgets_stacked_widget_example PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_widgets_stacked_widget_example PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_widgets_stacked_widget_example"
    )
endif()

add_executable(snf_widgets_layouts_example
    layouts_example.cpp
)

target_link_libraries(snf_widgets_layouts_example PRIVATE SNFWidgets)

target_compile_options(snf_widgets_layouts_example PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_widgets_layouts_example PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_widgets_layouts_example PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_widgets_layouts_example"
    )
endif()

add_executable(snf_widgets_vbox_stretch_scroll_example
    vbox_stretch_scroll_example.cpp
)

target_link_libraries(snf_widgets_vbox_stretch_scroll_example PRIVATE SNFWidgets)

target_compile_options(snf_widgets_vbox_stretch_scroll_example PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_widgets_vbox_stretch_scroll_example PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(snf_widgets_vbox_stretch_scroll_example PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_widgets_vbox_stretch_scroll_example"
    )
endif()
