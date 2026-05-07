add_executable(SNFNetworkTests
    "${CMAKE_CURRENT_SOURCE_DIR}/HostAddressTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TcpSocketTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TcpServerTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LocalSocketTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LocalServerTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UdpSocketTests.cpp"
)

target_link_libraries(SNFNetworkTests
    PRIVATE
        SNFNetwork
        GTest::gtest_main
)

target_include_directories(SNFNetworkTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

include(GoogleTest)
gtest_discover_tests(SNFNetworkTests TEST_PREFIX "SNFNetworkTests.")
