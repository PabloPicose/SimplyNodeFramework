cmake_minimum_required(VERSION 3.26)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SNFWEBSOCKET_DIR "${SRC_DIR}/SNFWebSocket")

add_library(SNFWebSocket STATIC)

target_sources(SNFWebSocket
    PRIVATE
        "${SNFWEBSOCKET_DIR}/WebSocket.cpp"
        "${SNFWEBSOCKET_DIR}/WebSocketServer.cpp"
        "${SNFWEBSOCKET_DIR}/WebSocketFrame.cpp"
        "${SNFWEBSOCKET_DIR}/WebSocketHandshake.cpp"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${SRC_DIR}"
        FILES
            "${SNFWEBSOCKET_DIR}/WebSocket.h"
            "${SNFWEBSOCKET_DIR}/WebSocketServer.h"
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

if(SNF_ENABLE_TESTS)
    add_subdirectory(test)
endif()
