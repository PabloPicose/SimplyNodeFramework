cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_subdirectory("${SRC_DIR}")

add_library(SNFHttpServer STATIC)

target_sources(SNFHttpServer
    PRIVATE
        ${SNFHTTPSERVER_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            ${SNFHTTPSERVER_HEADERS}
)

target_link_libraries(SNFHttpServer
    PUBLIC
        SNFCore
        SNFNetwork
)

target_include_directories(SNFHttpServer
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(SNFHttpServer PUBLIC cxx_std_17)

add_library(SNFHttpServer::SNFHttpServer ALIAS SNFHttpServer)

install(TARGETS SNFHttpServer
    EXPORT SNFHttpServerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFHttpServerTargets
    FILE SNFHttpServerTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFHttpServer
)

if (SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
