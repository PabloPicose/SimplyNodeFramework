add_executable(SNFWidgetsTests
    WidgetTests.cpp
    LabelTests.cpp
    PushButtonTests.cpp
    LayoutTests.cpp
    SplitterTests.cpp
    WindowTests.cpp
    MessageBoxTests.cpp
    CheckBoxTests.cpp
    RadioButtonTests.cpp
    SliderTests.cpp
    SpinBoxTests.cpp
    LineEditTests.cpp
    TextEditTests.cpp
    ComboBoxTests.cpp
    ProgressBarTests.cpp
    TableViewTests.cpp
    DataWidgetMapperTests.cpp
)

target_link_libraries(SNFWidgetsTests
    PRIVATE
        SNFWidgets
        GTest::gtest_main
)

target_include_directories(SNFWidgetsTests
    PRIVATE
        ${snf_imgui_SOURCE_DIR}
)

target_compile_features(SNFWidgetsTests PRIVATE cxx_std_17)

add_test(NAME SNFWidgetsTests COMMAND SNFWidgetsTests)
