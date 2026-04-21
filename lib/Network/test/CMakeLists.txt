add_executable(SNFNetworkTests
    "${CMAKE_CURRENT_SOURCE_DIR}/TcpSocketTests.cpp"
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

add_test(NAME SNFNetworkTests COMMAND SNFNetworkTests)
set_tests_properties(SNFNetworkTests PROPERTIES TIMEOUT 10)
