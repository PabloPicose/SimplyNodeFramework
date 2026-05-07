add_executable(SNFWebSocketTests
    WebSocketFrameTests.cpp
    WebSocketHandshakeTests.cpp
    WebSocketTests.cpp
    WebSocketServerTests.cpp
)

target_link_libraries(SNFWebSocketTests
    PRIVATE
        SNFWebSocket
        SNFNetwork
        GTest::gtest_main
)

target_include_directories(SNFWebSocketTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

include(GoogleTest)
gtest_discover_tests(SNFWebSocketTests TEST_PREFIX "SNFWebSocketTests.")
