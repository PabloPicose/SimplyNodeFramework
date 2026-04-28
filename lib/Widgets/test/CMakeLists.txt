add_executable(SNFWidgetsTests
    LabelTests.cpp
    LayoutTests.cpp
    CheckBoxTests.cpp
    RadioButtonTests.cpp
    SliderTests.cpp
    SpinBoxTests.cpp
    LineEditTests.cpp
    TextEditTests.cpp
    ComboBoxTests.cpp
    ProgressBarTests.cpp
    TableViewTests.cpp
)

target_link_libraries(SNFWidgetsTests
    PRIVATE
        SNFWidgets
        GTest::gtest_main
)

target_compile_features(SNFWidgetsTests PRIVATE cxx_std_17)

add_test(NAME SNFWidgetsTests COMMAND SNFWidgetsTests)
