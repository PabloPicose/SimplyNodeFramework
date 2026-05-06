add_executable(SNFExperimentalTests
    AsyncTaskSequenceTests.cpp
)

target_link_libraries(SNFExperimentalTests
    PRIVATE
        SNFExperimental
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(SNFExperimentalTests)
