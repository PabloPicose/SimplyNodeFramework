add_executable(SNFCoreTests
    CoreTests.cpp
    ConnectionTests.cpp
    TimerTests.cpp
)

target_link_libraries(SNFCoreTests
    PRIVATE
        SNFCore
        GTest::gtest_main
)

add_test(NAME SNFCoreTests COMMAND SNFCoreTests)
set_tests_properties(SNFCoreTests PROPERTIES TIMEOUT 10)
