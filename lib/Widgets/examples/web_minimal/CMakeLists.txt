cmake_minimum_required(VERSION 3.26)

add_executable(snf_web_minimal
    main.cpp
)

target_link_libraries(snf_web_minimal PRIVATE SNFWidgets)

target_compile_options(snf_web_minimal PRIVATE
    -Wall
    -Wextra
)

if(EMSCRIPTEN)
    target_link_options(snf_web_minimal PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
        --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/../../shell.html
    )

    set_target_properties(snf_web_minimal PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web/snf_web_minimal"
    )
endif()
