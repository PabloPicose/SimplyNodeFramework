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
        gtest
        gtest_main
)

target_include_directories(SNFWebSocketTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_compile_features(SNFWebSocketTests PRIVATE cxx_std_17)

add_test(
    NAME SNFWebSocketTests
    COMMAND SNFWebSocketTests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

include(GoogleTest)
gtest_discover_tests(SNFWebSocketTests)
