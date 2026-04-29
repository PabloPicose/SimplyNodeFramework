add_executable(webclient webclient-main.cpp)

# SNFWidgets::SNFWidgets transitively provides SNFCore, ImGui, and the
# Emscripten GLFW3 port link/compile options.
target_link_libraries(webclient PRIVATE 
    SNFWidgets::SNFWidgets 
    SNFWebSocket::SNFWebSocket
)

target_compile_options(webclient PRIVATE -Wall -Wextra)

if(EMSCRIPTEN)
    set(SNF_STANDALONE_WEB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../web")

    target_link_options(webclient PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNF_STANDALONE_EMSCRIPTEN_SHELL_FILE}
    )

    set_target_properties(webclient PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${SNF_STANDALONE_WEB_DIR}"
    )
endif()
