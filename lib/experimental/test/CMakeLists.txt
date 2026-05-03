add_executable(SNFExperimentalTests
    AsyncTaskSequenceTests.cpp
)

target_link_libraries(SNFExperimentalTests
    PRIVATE
        SNFExperimental
        GTest::gtest_main
)

add_test(NAME SNFExperimentalTests COMMAND SNFExperimentalTests)
