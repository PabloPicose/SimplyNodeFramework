#include <gtest/gtest.h>
#include "SNFWidgets/ProgressBar.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

class ProgressBarFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(ProgressBarFixture, defaultConstruction)
{
    ProgressBar bar;
    EXPECT_EQ(bar.minimum(), 0);
    EXPECT_EQ(bar.maximum(), 100);
    EXPECT_EQ(bar.value(), 0);
    EXPECT_EQ(bar.overlayText(), "");
}

TEST_F(ProgressBarFixture, constructionCustomRange)
{
    ProgressBar bar(10, 200);
    EXPECT_EQ(bar.minimum(), 10);
    EXPECT_EQ(bar.maximum(), 200);
    EXPECT_EQ(bar.value(), 10);
}

TEST_F(ProgressBarFixture, setValue)
{
    ProgressBar bar(0, 100);
    bar.setValue(75);
    EXPECT_EQ(bar.value(), 75);
}

TEST_F(ProgressBarFixture, setValueClampBelow)
{
    ProgressBar bar(0, 100);
    bar.setValue(-10);
    EXPECT_EQ(bar.value(), 0);
}

TEST_F(ProgressBarFixture, setValueClampAbove)
{
    ProgressBar bar(0, 100);
    bar.setValue(200);
    EXPECT_EQ(bar.value(), 100);
}

TEST_F(ProgressBarFixture, setRange)
{
    ProgressBar bar(0, 100);
    bar.setValue(80);
    bar.setRange(0, 50);
    EXPECT_EQ(bar.maximum(), 50);
    EXPECT_EQ(bar.value(), 50);  // Re-clamped.
}

TEST_F(ProgressBarFixture, invalidRangeSwapped)
{
    ProgressBar bar(100, 0);
    EXPECT_EQ(bar.minimum(), 0);
    EXPECT_EQ(bar.maximum(), 100);
}

TEST_F(ProgressBarFixture, minimumEqualsMaximum)
{
    // Edge case: min == max. setValue is accepted (same as min).
    ProgressBar bar(50, 50);
    EXPECT_EQ(bar.minimum(), 50);
    EXPECT_EQ(bar.maximum(), 50);
    bar.setValue(50);
    EXPECT_EQ(bar.value(), 50);
}

TEST_F(ProgressBarFixture, overlayText)
{
    ProgressBar bar(0, 100);
    bar.setOverlayText("50 %");
    EXPECT_EQ(bar.overlayText(), "50 %");
}

TEST_F(ProgressBarFixture, overlayTextEmpty)
{
    ProgressBar bar(0, 100);
    bar.setOverlayText("text");
    bar.setOverlayText("");
    EXPECT_EQ(bar.overlayText(), "");
}
