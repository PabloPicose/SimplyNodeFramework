add_executable(SNFSnmpTests
    "${CMAKE_CURRENT_SOURCE_DIR}/SnmpOidTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SnmpValueTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SnmpV3SessionTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SnmpTrapReceiverTests.cpp"
)

target_link_libraries(SNFSnmpTests
    PRIVATE
        SNFSnmp
        GTest::gtest_main
)

target_include_directories(SNFSnmpTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

include(GoogleTest)
gtest_discover_tests(SNFSnmpTests TEST_PREFIX "SNFSnmpTests.")
