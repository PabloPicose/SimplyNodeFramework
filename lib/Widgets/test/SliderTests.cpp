#include <gtest/gtest.h>
#include "SNFWidgets/Slider.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

class SliderFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(SliderFixture, defaultRange)
{
    Slider sld("Vol", 0, 100);
    EXPECT_EQ(sld.minimum(), 0);
    EXPECT_EQ(sld.maximum(), 100);
    EXPECT_EQ(sld.value(), 0);
}

TEST_F(SliderFixture, setLabel)
{
    Slider sld("Old", 0, 10);
    sld.setLabel("New");
    EXPECT_EQ(sld.label(), "New");
}

TEST_F(SliderFixture, setValue)
{
    Slider sld("s", 0, 100);
    sld.setValue(42);
    EXPECT_EQ(sld.value(), 42);
}

TEST_F(SliderFixture, setValueClampBelow)
{
    Slider sld("s", 0, 100);
    sld.setValue(-5);
    EXPECT_EQ(sld.value(), 0);
}

TEST_F(SliderFixture, setValueClampAbove)
{
    Slider sld("s", 0, 100);
    sld.setValue(200);
    EXPECT_EQ(sld.value(), 100);
}

TEST_F(SliderFixture, setRange)
{
    Slider sld("s", 0, 100);
    sld.setValue(80);
    sld.setRange(10, 50);
    EXPECT_EQ(sld.minimum(), 10);
    EXPECT_EQ(sld.maximum(), 50);
    // Value clamped after range change.
    EXPECT_EQ(sld.value(), 50);
}

TEST_F(SliderFixture, invalidRangeSwapped)
{
    // minimum > maximum: endpoints swapped automatically.
    Slider sld("s", 100, 0);
    EXPECT_EQ(sld.minimum(), 0);
    EXPECT_EQ(sld.maximum(), 100);
}

TEST_F(SliderFixture, setRangeInvalidSwapped)
{
    Slider sld("s", 0, 100);
    sld.setRange(90, 10);
    EXPECT_EQ(sld.minimum(), 10);
    EXPECT_EQ(sld.maximum(), 90);
}

// Programmatic setter does NOT emit valueChanged.
TEST_F(SliderFixture, setterDoesNotEmitSignal)
{
    Slider sld("s", 0, 100);
    int count = 0;
    sld.valueChanged.connect([&](int) { ++count; });
    sld.setValue(50);
    EXPECT_EQ(count, 0);
}
