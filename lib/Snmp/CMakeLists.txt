cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ── Locate net-snmp ───────────────────────────────────────────────────────────
# Try pkg-config first, then fall back to manual library search.
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(NETSNMP QUIET netsnmp)
endif()

if(NOT NETSNMP_FOUND)
    find_library(NETSNMP_LIB_PATH NAMES netsnmp)
    find_path(NETSNMP_INCLUDE_PATH NAMES net-snmp/net-snmp-config.h)

    if(NETSNMP_LIB_PATH AND NETSNMP_INCLUDE_PATH)
        set(NETSNMP_FOUND TRUE)
        set(NETSNMP_LIBRARIES     "${NETSNMP_LIB_PATH}")
        set(NETSNMP_INCLUDE_DIRS  "${NETSNMP_INCLUDE_PATH}")
        set(NETSNMP_LINK_LIBRARIES "${NETSNMP_LIB_PATH}")
    endif()
endif()

if(NOT NETSNMP_FOUND)
    message(WARNING
        "net-snmp not found — SNFSnmp target will not be built.\n"
        "Install net-snmp (e.g. 'sudo apt install libsnmp-dev') and re-configure.")
    return()
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
add_subdirectory("${SRC_DIR}")

# ── Library target ────────────────────────────────────────────────────────────
add_library(SNFSnmp STATIC)

target_sources(SNFSnmp
    PRIVATE
        ${SNFSNMP_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            ${SNFSNMP_HEADERS}
)

target_link_libraries(SNFSnmp
    PUBLIC
        SNFCore
    PRIVATE
        ${NETSNMP_LINK_LIBRARIES}
)

target_include_directories(SNFSnmp
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${NETSNMP_INCLUDE_DIRS}
)

target_compile_features(SNFSnmp PUBLIC cxx_std_17)

add_library(SNFSnmp::SNFSnmp ALIAS SNFSnmp)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS SNFSnmp
    EXPORT SNFSnmpTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFSnmpTargets
    FILE SNFSnmpTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFSnmp
)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
