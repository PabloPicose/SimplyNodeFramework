#include <gtest/gtest.h>
#include "SNFWidgets/SpinBox.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

class SpinBoxFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(SpinBoxFixture, defaultState)
{
    SpinBox spin("Count", 0, 100);
    EXPECT_EQ(spin.minimum(), 0);
    EXPECT_EQ(spin.maximum(), 100);
    EXPECT_EQ(spin.value(), 0);
    EXPECT_EQ(spin.step(), 1);
}

TEST_F(SpinBoxFixture, setLabel)
{
    SpinBox spin("Old", 0, 10);
    spin.setLabel("New");
    EXPECT_EQ(spin.label(), "New");
}

TEST_F(SpinBoxFixture, setValue)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(30);
    EXPECT_EQ(spin.value(), 30);
}

TEST_F(SpinBoxFixture, setValueClampBelow)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(-1);
    EXPECT_EQ(spin.value(), 0);
}

TEST_F(SpinBoxFixture, setValueClampAbove)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(150);
    EXPECT_EQ(spin.value(), 100);
}

TEST_F(SpinBoxFixture, setRange)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(80);
    spin.setRange(5, 50);
    EXPECT_EQ(spin.minimum(), 5);
    EXPECT_EQ(spin.maximum(), 50);
    EXPECT_EQ(spin.value(), 50);
}

TEST_F(SpinBoxFixture, invalidRangeSwapped)
{
    SpinBox spin("s", 100, 0);
    EXPECT_EQ(spin.minimum(), 0);
    EXPECT_EQ(spin.maximum(), 100);
}

TEST_F(SpinBoxFixture, setStep)
{
    SpinBox spin("s", 0, 100);
    spin.setStep(5);
    EXPECT_EQ(spin.step(), 5);
}

TEST_F(SpinBoxFixture, setStepZeroIgnored)
{
    SpinBox spin("s", 0, 100);
    spin.setStep(3);
    spin.setStep(0);   // Should be ignored.
    EXPECT_EQ(spin.step(), 3);
}

TEST_F(SpinBoxFixture, setStepNegativeIgnored)
{
    SpinBox spin("s", 0, 100);
    spin.setStep(2);
    spin.setStep(-1);  // Should be ignored.
    EXPECT_EQ(spin.step(), 2);
}

// Programmatic setter does NOT emit valueChanged.
TEST_F(SpinBoxFixture, setterDoesNotEmitSignal)
{
    SpinBox spin("s", 0, 100);
    int count = 0;
    spin.valueChanged.connect([&](int) { ++count; });
    spin.setValue(50);
    EXPECT_EQ(count, 0);
}
