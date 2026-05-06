add_executable(SNFJsonTests
    "${CMAKE_CURRENT_SOURCE_DIR}/JsonArrayTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/JsonDocumentTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/JsonObjectTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/JsonValueTests.cpp"
)

target_link_libraries(SNFJsonTests
    PRIVATE
        SNFJson
        GTest::gtest_main
)

target_include_directories(SNFJsonTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

include(GoogleTest)
gtest_discover_tests(SNFJsonTests)
