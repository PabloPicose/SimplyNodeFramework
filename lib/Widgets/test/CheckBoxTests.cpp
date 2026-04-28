#include <gtest/gtest.h>
#include "SNFWidgets/CheckBox.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

class CheckBoxFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(CheckBoxFixture, defaultState)
{
    CheckBox chk("Enable");
    EXPECT_EQ(chk.label(), "Enable");
    EXPECT_FALSE(chk.isChecked());
}

TEST_F(CheckBoxFixture, setCheckedTrue)
{
    CheckBox chk("Enable");
    chk.setChecked(true);
    EXPECT_TRUE(chk.isChecked());
}

TEST_F(CheckBoxFixture, setCheckedFalse)
{
    CheckBox chk("Enable");
    chk.setChecked(true);
    chk.setChecked(false);
    EXPECT_FALSE(chk.isChecked());
}

TEST_F(CheckBoxFixture, setLabel)
{
    CheckBox chk("Old");
    chk.setLabel("New");
    EXPECT_EQ(chk.label(), "New");
}

// Programmatic setter does NOT emit stateChanged.
TEST_F(CheckBoxFixture, setterDoesNotEmitSignal)
{
    CheckBox chk("x");
    int count = 0;
    chk.stateChanged.connect([&](bool) { ++count; });
    chk.setChecked(true);
    chk.setChecked(false);
    EXPECT_EQ(count, 0);
}
