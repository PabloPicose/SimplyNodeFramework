cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFWEBSOCKET_DIR "${SRC_DIR}/SNFWebSocket")

add_library(SNFWebSocket STATIC)

set(SNFWEBSOCKET_PRIVATE_SOURCES
    "${SNFWEBSOCKET_DIR}/WebSocket.cpp"
)

set(SNFWEBSOCKET_PUBLIC_HEADERS
    "${SNFWEBSOCKET_DIR}/WebSocket.h"
)

if(SNF_WEB_ASSEMBLY)
    list(APPEND SNFWEBSOCKET_PRIVATE_SOURCES
        "${SNFWEBSOCKET_DIR}/WebSocketImplEmscripten.cpp"
    )
else()
    list(APPEND SNFWEBSOCKET_PRIVATE_SOURCES
        "${SNFWEBSOCKET_DIR}/WebSocketImplNative.cpp"
        "${SNFWEBSOCKET_DIR}/WebSocketServer.cpp"
        "${SNFWEBSOCKET_DIR}/WebSocketFrame.cpp"
        "${SNFWEBSOCKET_DIR}/WebSocketHandshake.cpp"
    )
    list(APPEND SNFWEBSOCKET_PUBLIC_HEADERS
        "${SNFWEBSOCKET_DIR}/WebSocketServer.h"
    )
endif()

target_sources(SNFWebSocket
    PRIVATE
        ${SNFWEBSOCKET_PRIVATE_SOURCES}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            ${SNFWEBSOCKET_PUBLIC_HEADERS}
)

target_link_libraries(SNFWebSocket
    PUBLIC
        SNFCore
        SNFNetwork
)

target_include_directories(SNFWebSocket
    PUBLIC
        $<BUILD_INTERFACE:${SRC_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(SNFWebSocket PUBLIC cxx_std_17)

if(SNF_WEB_ASSEMBLY)
    target_link_options(SNFWebSocket INTERFACE -lwebsocket.js)
endif()

add_library(SNFWebSocket::SNFWebSocket ALIAS SNFWebSocket)

install(TARGETS SNFWebSocket
    EXPORT SNFWebSocketTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT SNFWebSocketTargets
    FILE SNFWebSocketTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNFWebSocket
)

if(SNF_ENABLE_TESTS AND NOT SNF_WEB_ASSEMBLY)
    add_subdirectory(test)
endif()
