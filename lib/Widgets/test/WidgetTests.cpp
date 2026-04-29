#include <gtest/gtest.h>

#include "SNFWidgets/Label.h"
#include "SNFWidgets/Layout.h"
#include "SNFWidgets/PushButton.h"
#include "SNFWidgets/Window.h"

#include <SNFCore/Application.h>

using namespace snf::widgets;

class WidgetFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new snf::Application(0, nullptr); }
    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

    snf::Application* app = nullptr;
};

TEST_F(WidgetFixture, widgetsAreEnabledByDefault)
{
    Label label("Status");

    EXPECT_TRUE(label.isEnabled());
    EXPECT_TRUE(label.isEffectivelyEnabled());
}

TEST_F(WidgetFixture, setEnabledAndSetDisabledControlLocalState)
{
    PushButton button("Apply");

    button.setEnabled(false);
    EXPECT_FALSE(button.isEnabled());
    EXPECT_FALSE(button.isEffectivelyEnabled());

    button.setDisabled(false);
    EXPECT_TRUE(button.isEnabled());
    EXPECT_TRUE(button.isEffectivelyEnabled());

    button.setDisabled(true);
    EXPECT_FALSE(button.isEnabled());
    EXPECT_FALSE(button.isEffectivelyEnabled());
}

TEST_F(WidgetFixture, childInheritsDisabledStateFromWidgetParent)
{
    Window window("Settings");
    PushButton button("Apply", &window);

    EXPECT_TRUE(button.isEffectivelyEnabled());

    window.setEnabled(false);

    EXPECT_TRUE(button.isEnabled());
    EXPECT_FALSE(window.isEffectivelyEnabled());
    EXPECT_FALSE(button.isEffectivelyEnabled());
}

TEST_F(WidgetFixture, childInheritsDisabledStateFromLayoutParent)
{
    VBoxLayout layout;
    PushButton button("Apply");
    layout.addWidget(&button);

    EXPECT_TRUE(button.isEffectivelyEnabled());

    layout.setDisabled(true);

    EXPECT_TRUE(button.isEnabled());
    EXPECT_FALSE(layout.isEffectivelyEnabled());
    EXPECT_FALSE(button.isEffectivelyEnabled());
}

TEST_F(WidgetFixture, reenablingParentRestoresEffectiveState)
{
    Window window("Settings");
    PushButton button("Apply", &window);

    window.setDisabled(true);
    EXPECT_FALSE(button.isEffectivelyEnabled());

    window.setEnabled(true);
    EXPECT_TRUE(button.isEffectivelyEnabled());
}
