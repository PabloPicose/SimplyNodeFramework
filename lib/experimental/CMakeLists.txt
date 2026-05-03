cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFEXPERIMENTAL_DIR "${SRC_DIR}/SNFExperimental")

add_library(SNFExperimental STATIC)

target_sources(SNFExperimental
    PRIVATE
        "${SNFEXPERIMENTAL_DIR}/AsyncTask.cpp"
        "${SNFEXPERIMENTAL_DIR}/AsyncTaskSequence.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            "${SNFEXPERIMENTAL_DIR}/AsyncTask.h"
            "${SNFEXPERIMENTAL_DIR}/AsyncTaskSequence.h"
)

target_link_libraries(SNFExperimental
    PUBLIC
        SNFCore
)

target_include_directories(SNFExperimental
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(SNFExperimental PUBLIC cxx_std_17)

add_library(SNFExperimental::SNFExperimental ALIAS SNFExperimental)

if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
