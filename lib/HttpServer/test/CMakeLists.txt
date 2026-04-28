add_executable(SNFHttpServerTests
    HttpRequestParserTests.cpp
    HttpServerTests.cpp
)

target_link_libraries(SNFHttpServerTests
    PRIVATE
        SNFHttpServer
        gtest
        gtest_main
)

target_compile_features(SNFHttpServerTests PRIVATE cxx_std_17)

add_test(
    NAME SNFHttpServerTests
    COMMAND SNFHttpServerTests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
