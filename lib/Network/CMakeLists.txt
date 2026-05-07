cmake_minimum_required(VERSION 3.26)

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
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(SNFNetwork PUBLIC cxx_std_17)

add_library(SNFNetwork::SNFNetwork ALIAS SNFNetwork)

install(TARGETS SNFNetwork
    EXPORT SNFNetworkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFNetworkTargets
    FILE SNFNetworkTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFNetwork
)

if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()

