#include <gtest/gtest.h>
#include "SNFWidgets/RadioButton.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

class RadioButtonFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(RadioButtonFixture, defaultState)
{
    RadioButton rb("Option A");
    EXPECT_EQ(rb.label(), "Option A");
    EXPECT_FALSE(rb.isSelected());
}

TEST_F(RadioButtonFixture, setSelectedTrue)
{
    RadioButton rb("Option A");
    rb.setSelected(true);
    EXPECT_TRUE(rb.isSelected());
}

TEST_F(RadioButtonFixture, setSelectedFalse)
{
    RadioButton rb("Option A");
    rb.setSelected(true);
    rb.setSelected(false);
    EXPECT_FALSE(rb.isSelected());
}

TEST_F(RadioButtonFixture, setLabel)
{
    RadioButton rb("Old");
    rb.setLabel("New");
    EXPECT_EQ(rb.label(), "New");
}

// Programmatic setter does NOT emit stateChanged.
TEST_F(RadioButtonFixture, setterDoesNotEmitSignal)
{
    RadioButton rb("x");
    int count = 0;
    rb.stateChanged.connect([&](bool) { ++count; });
    rb.setSelected(true);
    rb.setSelected(false);
    EXPECT_EQ(count, 0);
}
