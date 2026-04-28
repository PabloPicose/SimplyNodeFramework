cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFHTTPSERVER_DIR "${SRC_DIR}/SNFHttpServer")

add_library(SNFHttpServer STATIC)

target_sources(SNFHttpServer
    PRIVATE
        "${SNFHTTPSERVER_DIR}/HttpRequest.cpp"
        "${SNFHTTPSERVER_DIR}/HttpResponse.cpp"
        "${SNFHTTPSERVER_DIR}/HttpRequestParser.cpp"
        "${SNFHTTPSERVER_DIR}/HttpServer.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            "${SNFHTTPSERVER_DIR}/HttpRequest.h"
            "${SNFHTTPSERVER_DIR}/HttpResponse.h"
            "${SNFHTTPSERVER_DIR}/HttpRequestParser.h"
            "${SNFHTTPSERVER_DIR}/HttpServer.h"
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
