add_executable(snf_standalone_app main.cpp)

# SNFWidgets::SNFWidgets transitively provides SNFCore, ImGui, and the
# Emscripten GLFW3 port link/compile options.
target_link_libraries(snf_standalone_app PRIVATE SNFWidgets::SNFWidgets)

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
