cmake_minimum_required(VERSION 3.26)

add_executable(SNFCoreTests
    CoreTests.cpp
    ConnectionTests.cpp
    TimerTests.cpp
    CommandLineParserTests.cpp
    IniParserTests.cpp
    AbstractTableModelTests.cpp
    VariantTests.cpp
    RunnableTests.cpp
    ThreadPoolTests.cpp
    FileTests.cpp
    TempFileTests.cpp
    ByteArrayTests.cpp
    SpanTests.cpp
    LoggingTests.cpp
    ProcessTests.cpp
    ThreadStorageTests.cpp
    TimeLapseTests.cpp
)

target_link_libraries(SNFCoreTests
    PRIVATE
        SNFCore
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(SNFCoreTests TEST_PREFIX "SNFCoreTests.")
