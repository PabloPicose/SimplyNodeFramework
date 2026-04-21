cmake_minimum_required(VERSION 3.26)
project(SNFNetwork)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_subdirectory("${SRC_DIR}")

add_library(SNFNetwork STATIC)

target_sources(SNFNetwork
    PRIVATE
        ${SNFNETWORK_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            ${SNFNETWORK_HEADERS}
)

target_link_libraries(SNFNetwork
    PUBLIC
        SNFCore
)

target_include_directories(SNFNetwork
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:include>
)

install(TARGETS SNFNetwork
    ARCHIVE DESTINATION lib
    FILE_SET HEADERS DESTINATION include
)

if (ENABLE_TEST)
    add_subdirectory(test)
endif()

