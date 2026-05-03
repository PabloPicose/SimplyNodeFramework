cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFDATABASE_DIR "${SRC_DIR}/SNFDatabase")

add_library(SNFDatabase STATIC)

set(SNFDATABASE_SOURCES
    "${SNFDATABASE_DIR}/SqlDatabase.cpp"
    "${SNFDATABASE_DIR}/SqlQuery.cpp"
)

set(SNFDATABASE_HEADERS
    "${SNFDATABASE_DIR}/SqlDatabase.h"
    "${SNFDATABASE_DIR}/SqlQuery.h"
)

if(SNF_ENABLE_SQLITE)
    find_package(SQLite3 REQUIRED)

    list(APPEND SNFDATABASE_SOURCES
        "${SNFDATABASE_DIR}/SqliteDatabase.cpp"
    )

    list(APPEND SNFDATABASE_HEADERS
        "${SNFDATABASE_DIR}/SqliteDatabase.h"
    )

    target_compile_definitions(SNFDatabase PUBLIC SNF_ENABLE_SQLITE)
    target_link_libraries(SNFDatabase PRIVATE SQLite::SQLite3)
endif()

target_sources(SNFDatabase
    PRIVATE
        ${SNFDATABASE_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            ${SNFDATABASE_HEADERS}
)

target_link_libraries(SNFDatabase
    PUBLIC
        SNFCore
)

target_include_directories(SNFDatabase
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(SNFDatabase PUBLIC cxx_std_17)

add_library(SNFDatabase::SNFDatabase ALIAS SNFDatabase)

install(TARGETS SNFDatabase
    EXPORT SNFDatabaseTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFDatabaseTargets
    FILE SNFDatabaseTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFDatabase
)

if(SNF_ENABLE_TESTS AND SNF_ENABLE_SQLITE)
    add_subdirectory(test)
endif()
