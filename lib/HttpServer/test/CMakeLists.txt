add_executable(SNFHttpServerTests
    HttpRequestParserTests.cpp
    HttpServerTests.cpp
)

target_link_libraries(SNFHttpServerTests
    PRIVATE
        SNFHttpServer
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(SNFHttpServerTests TEST_PREFIX "SNFHttpServerTests.")
