cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFDATABASE_DIR "${SRC_DIR}/SNFDatabase")

add_library(SNFDatabase STATIC)

set(SNFDATABASE_SOURCES
    "${SNFDATABASE_DIR}/SqlDatabase.cpp"
    "${SNFDATABASE_DIR}/SqlQuery.cpp"
    "${SNFDATABASE_DIR}/SqlTableModel.cpp"
)

set(SNFDATABASE_HEADERS
    "${SNFDATABASE_DIR}/SqlDatabase.h"
    "${SNFDATABASE_DIR}/SqlDatabaseProvider.h"
    "${SNFDATABASE_DIR}/SqlQuery.h"
    "${SNFDATABASE_DIR}/SqlTableModel.h"
    "${SNFDATABASE_DIR}/MySqlDatabaseManager.h"
)

if(SNF_ENABLE_SQLITE)
    find_package(SQLite3 QUIET)
    if(NOT TARGET SQLite3::SQLite3)
        # Fallback: some distros ship a SQLite3Config.cmake that sets SQLite3_FOUND
        # but does not define the imported target. Use pkg-config to fill the gap.
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SQLITE3_PC REQUIRED IMPORTED_TARGET sqlite3)
        add_library(_SNFSQLite3 INTERFACE)
        target_link_libraries(_SNFSQLite3 INTERFACE PkgConfig::SQLITE3_PC)
        add_library(SQLite3::SQLite3 ALIAS _SNFSQLite3)
    endif()

    list(APPEND SNFDATABASE_SOURCES
        "${SNFDATABASE_DIR}/SqliteDatabase.cpp"
    )

    list(APPEND SNFDATABASE_HEADERS
        "${SNFDATABASE_DIR}/SqliteDatabase.h"
    )

    target_compile_definitions(SNFDatabase PUBLIC SNF_ENABLE_SQLITE)
    target_link_libraries(SNFDatabase PRIVATE SQLite3::SQLite3)
endif()

if(SNF_ENABLE_MYSQL)
    find_package(PkgConfig REQUIRED)
    foreach(_snf_mysql_pc mysqlclient libmariadb mariadb)
        pkg_check_modules(MYSQLCLIENT IMPORTED_TARGET ${_snf_mysql_pc})
        if(MYSQLCLIENT_FOUND)
            break()
        endif()
    endforeach()
    if(NOT MYSQLCLIENT_FOUND)
        message(FATAL_ERROR "SNF_ENABLE_MYSQL is ON but no MySQL/MariaDB client library was found (tried: mysqlclient, libmariadb, mariadb)")
    endif()

    list(APPEND SNFDATABASE_SOURCES
        "${SNFDATABASE_DIR}/MySqlDatabase.cpp"
    )

    list(APPEND SNFDATABASE_HEADERS
        "${SNFDATABASE_DIR}/MySqlDatabase.h"
    )

    target_compile_definitions(SNFDatabase PUBLIC SNF_ENABLE_MYSQL)
    target_link_libraries(SNFDatabase PRIVATE PkgConfig::MYSQLCLIENT)
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

if(SNF_ENABLE_TESTS AND (SNF_ENABLE_SQLITE OR SNF_ENABLE_MYSQL))
    add_subdirectory(test)
endif()
