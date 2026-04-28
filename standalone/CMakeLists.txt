cmake_minimum_required(VERSION 3.26)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(snf_standalone_examples VERSION 1.0.0 LANGUAGES CXX)
endif()

if(NOT TARGET SNFCore OR NOT TARGET SNFWidgets)
    message(FATAL_ERROR
        "standalone must be built from the SimplyNodeFramework root CMake project. "
        "Configure the repository root instead of standalone/."
    )
endif()

set(SNF_STANDALONE_EMSCRIPTEN_SHELL_FILE
    "${PROJECT_SOURCE_DIR}/lib/Widgets/shell.html"
)

add_subdirectory(app/webclient)

if(NOT SNF_WEB_ASSEMBLY)
    if(NOT TARGET SNFHttpServer)
        message(FATAL_ERROR "standalone/http requires the SNFHttpServer target")
    endif()

    add_subdirectory(app/http)
endif()
