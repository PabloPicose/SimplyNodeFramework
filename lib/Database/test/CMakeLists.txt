add_executable(SNFDatabaseTests
    "${CMAKE_CURRENT_SOURCE_DIR}/SqliteDatabaseTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SqlTableModelTests.cpp"
)

target_link_libraries(SNFDatabaseTests
    PRIVATE
        SNFDatabase
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(SNFDatabaseTests TEST_PREFIX "SNFDatabaseTests.")
