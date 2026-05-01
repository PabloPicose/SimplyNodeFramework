cmake_minimum_required(VERSION 3.26)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(nlohmann_json)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFJSON_DIR "${SRC_DIR}/SNFJson")

add_library(SNFJson STATIC)

target_sources(SNFJson
    PRIVATE
        "${SNFJSON_DIR}/JsonArray.cpp"
        "${SNFJSON_DIR}/JsonDocument.cpp"
        "${SNFJSON_DIR}/JsonObject.cpp"
        "${SNFJSON_DIR}/JsonParseError.cpp"
        "${SNFJSON_DIR}/JsonValue.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            "${SNFJSON_DIR}/JsonArray.h"
            "${SNFJSON_DIR}/JsonDocument.h"
            "${SNFJSON_DIR}/JsonObject.h"
            "${SNFJSON_DIR}/JsonParseError.h"
            "${SNFJSON_DIR}/JsonValue.h"
)

target_link_libraries(SNFJson
    PUBLIC
        SNFCore
)

target_include_directories(SNFJson
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${nlohmann_json_SOURCE_DIR}/include"
)

target_compile_features(SNFJson PUBLIC cxx_std_17)

add_library(SNFJson::SNFJson ALIAS SNFJson)

install(TARGETS SNFJson
    EXPORT SNFJsonTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFJsonTargets
    FILE SNFJsonTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFJson
)

if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
