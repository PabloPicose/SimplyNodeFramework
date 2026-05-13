cmake_minimum_required(VERSION 3.26)

# ── Helper ────────────────────────────────────────────────────────────────────
# Adds one widget example executable and applies common compiler flags and
# Emscripten link options.
#
# Usage:
#   snf_add_widget_example(<target_name> <source_file> [<additional_libraries>...])
function(snf_add_widget_example target source)
    add_executable(${target} ${source})

    target_link_libraries(${target}
        PRIVATE
            SNFWidgets
            ${ARGN}
    )

    if(SNF_ENABLE_PROFILING)
        target_link_libraries(${target} PRIVATE SNFProfiler)
    endif()

    target_compile_options(${target} PRIVATE -Wall -Wextra)

    if(EMSCRIPTEN)
        target_link_options(${target} PRIVATE
            -sNO_FILESYSTEM=1
            -sDISABLE_EXCEPTION_CATCHING=1
            --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
        )

        set_target_properties(${target} PROPERTIES
            SUFFIX ".html"
            OUTPUT_NAME "index"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/${target}"
        )
    endif()
endfunction()

# ── Examples ──────────────────────────────────────────────────────────────────
snf_add_widget_example(snf_widgets_table_model_example     table_model_example.cpp)
snf_add_widget_example(snf_widgets_splitter_example        splitter_example.cpp)
snf_add_widget_example(snf_widgets_scroll_area_example     scroll_area_example.cpp)
snf_add_widget_example(snf_widgets_tabs_example            tabs_example.cpp)
snf_add_widget_example(snf_widgets_stacked_widget_example  stacked_widget_example.cpp)
snf_add_widget_example(snf_widgets_layouts_example         layouts_example.cpp)

# Link the echo to the Network library
snf_add_widget_example(snf_widgets_echo_client_example     echo_client_example.cpp SNFNetwork)
