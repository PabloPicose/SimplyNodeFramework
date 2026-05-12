set(SNFDATABASE_TEST_SOURCES)

if(SNF_ENABLE_SQLITE)
    list(APPEND SNFDATABASE_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/SqliteDatabaseTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SqlTableModelTests.cpp"
    )
endif()

if(SNF_ENABLE_MYSQL)
    list(APPEND SNFDATABASE_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/MySqlDatabaseTests.cpp"
    )
endif()

add_executable(SNFDatabaseTests ${SNFDATABASE_TEST_SOURCES})

target_link_libraries(SNFDatabaseTests
    PRIVATE
        SNFDatabase
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(SNFDatabaseTests
    TEST_PREFIX "SNFDatabaseTests."
    TEST_LIST SNFDATABASE_DISCOVERED_TESTS
)

foreach(test_name IN LISTS SNFDATABASE_DISCOVERED_TESTS)
    if(test_name MATCHES "^SNFDatabaseTests\\.MySqlDatabaseTest\\.")
        set_tests_properties(${test_name} PROPERTIES RESOURCE_LOCK "snf-mysql-schema")
    endif()
endforeach()
