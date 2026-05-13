set(SNFDATABASE_TEST_SOURCES)
set(SNFDATABASE_MYSQL_TEST_SOURCES)

if(SNF_ENABLE_SQLITE)
    list(APPEND SNFDATABASE_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/SqliteDatabaseTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SqlTableModelTests.cpp"
    )
endif()

if(SNF_ENABLE_MYSQL)
    list(APPEND SNFDATABASE_MYSQL_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/MySqlDatabaseTests.cpp"
    )
endif()

if(SNFDATABASE_TEST_SOURCES)
    add_executable(SNFDatabaseTests ${SNFDATABASE_TEST_SOURCES})

    target_link_libraries(SNFDatabaseTests
        PRIVATE
            SNFDatabase
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(SNFDatabaseTests
        TEST_PREFIX "SNFDatabaseTests."
    )
endif()

if(SNFDATABASE_MYSQL_TEST_SOURCES)
    add_executable(SNFDatabaseMySqlTests ${SNFDATABASE_MYSQL_TEST_SOURCES})

    target_link_libraries(SNFDatabaseMySqlTests
        PRIVATE
            SNFDatabase
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(SNFDatabaseMySqlTests
        TEST_PREFIX "SNFDatabaseTests."
        PROPERTIES RESOURCE_LOCK "snf-mysql-schema"
    )
endif()
