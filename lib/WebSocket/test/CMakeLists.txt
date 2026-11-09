add_executable(SNFWebSocketTests
    WebSocketFrameTests.cpp
    WebSocketHandshakeTests.cpp
    WebSocketTests.cpp
    WebSocketServerTests.cpp
    WebSocketProtocolTests.cpp
)

target_link_libraries(SNFWebSocketTests
    PRIVATE
        SNFWebSocket
        SNFNetwork
        GTest::gtest_main
)

# WebSocket served on the HttpServer port (HttpServer::upgrade + handleUpgrade).
if(TARGET SNFHttpServer)
    target_sources(SNFWebSocketTests PRIVATE WebSocketHttpUpgradeTests.cpp)
    target_link_libraries(SNFWebSocketTests PRIVATE SNFHttpServer)
endif()

target_include_directories(SNFWebSocketTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

include(GoogleTest)
gtest_discover_tests(SNFWebSocketTests TEST_PREFIX "SNFWebSocketTests.")
