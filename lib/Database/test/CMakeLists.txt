add_executable(SNFDatabaseTests
    "${CMAKE_CURRENT_SOURCE_DIR}/SqliteDatabaseTests.cpp"
)

target_link_libraries(SNFDatabaseTests
    PRIVATE
        SNFDatabase
        GTest::gtest_main
)

add_test(NAME SNFDatabaseTests COMMAND SNFDatabaseTests)
