cmake_minimum_required(VERSION 3.26)

add_executable(ems_save_client
    main.cpp
)

target_link_libraries(ems_save_client PRIVATE SNFWidgets SNFWebSocket)

target_compile_options(ems_save_client PRIVATE -Wall -Wextra)

if(EMSCRIPTEN)
    target_link_options(ems_save_client PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${SNFWIDGETS_EMSCRIPTEN_SHELL_FILE}
        # Export the malloc/free helpers used by snf_trigger_file_open_js and
        # the C callback that receives data from the browser FileReader.
        "SHELL:-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_snf_file_open_result"
        # Export the string helpers used inside EM_JS bodies.
        "SHELL:-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8"
    )

    set_target_properties(ems_save_client PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/ems_save_client"
    )
endif()
