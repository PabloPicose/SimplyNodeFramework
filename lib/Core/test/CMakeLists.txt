add_executable(SNFCoreTests
    CoreTests.cpp
    ConnectionTests.cpp
    TimerTests.cpp
    CommandLineParserTests.cpp
)

target_link_libraries(SNFCoreTests
    PRIVATE
        SNFCore
        GTest::gtest_main
)

add_test(NAME SNFCoreTests COMMAND SNFCoreTests)
