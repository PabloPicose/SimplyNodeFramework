#include <gtest/gtest.h>

#include "SNFWidgets/Window.h"

#include <SNFCore/Application.h>

using namespace snf::widgets;

class WindowFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }

    snf::Application* app = nullptr;
};

TEST_F(WindowFixture, defaultStateKeepsExistingWindowBehaviour)
{
    Window window("Settings");

    EXPECT_EQ(window.title(), "Settings");
    EXPECT_TRUE(window.isOpen());
    EXPECT_EQ(window.mode(), Window::Mode::Normal);
    EXPECT_TRUE(window.isCollapsible());
    EXPECT_TRUE(window.isResizable());
    EXPECT_TRUE(window.isMovable());
    EXPECT_TRUE(window.isTitleBarVisible());
    EXPECT_FALSE(window.isCloseButtonVisible());
    EXPECT_TRUE(window.isScrollEnabled());
    EXPECT_FALSE(window.isFullSize());
}

TEST_F(WindowFixture, initialSizeAndPosition)
{
    Window window("Settings");

    window.setInitialSize(640.0f, 480.0f);
    window.setInitialPosition(20.0f, 30.0f);

    EXPECT_TRUE(window.hasInitialSize());
    EXPECT_FLOAT_EQ(window.initialWidth(), 640.0f);
    EXPECT_FLOAT_EQ(window.initialHeight(), 480.0f);
    EXPECT_TRUE(window.hasInitialPosition());
    EXPECT_FLOAT_EQ(window.initialX(), 20.0f);
    EXPECT_FLOAT_EQ(window.initialY(), 30.0f);

    window.clearInitialSize();
    window.clearInitialPosition();

    EXPECT_FALSE(window.hasInitialSize());
    EXPECT_FALSE(window.hasInitialPosition());
}

TEST_F(WindowFixture, initialSizeRejectsNonPositiveDimensions)
{
    Window window("Settings");

    window.setInitialSize(-1.0f, 200.0f);

    EXPECT_FALSE(window.hasInitialSize());
    EXPECT_FLOAT_EQ(window.initialWidth(), 0.0f);
    EXPECT_FLOAT_EQ(window.initialHeight(), 200.0f);
}

TEST_F(WindowFixture, fullSizeConfiguration)
{
    Window window("Root");

    window.setFullSize(true);
    EXPECT_TRUE(window.isFullSize());
    EXPECT_TRUE(window.fullSizeHidesTitleBar());

    window.setFullSizeHidesTitleBar(false);
    EXPECT_FALSE(window.fullSizeHidesTitleBar());

    window.setFullSize(false);
    EXPECT_FALSE(window.isFullSize());
}

TEST_F(WindowFixture, convenienceFlags)
{
    Window window("Settings");

    window.setCollapsible(false);
    window.setResizable(false);
    window.setMovable(false);
    window.setTitleBarVisible(false);
    window.setScrollEnabled(false);
    window.setCloseButtonVisible(true);

    EXPECT_FALSE(window.isCollapsible());
    EXPECT_FALSE(window.isResizable());
    EXPECT_FALSE(window.isMovable());
    EXPECT_FALSE(window.isTitleBarVisible());
    EXPECT_FALSE(window.isScrollEnabled());
    EXPECT_TRUE(window.isCloseButtonVisible());

    EXPECT_TRUE(window.testFlag(Window::Flag::NoCollapse));
    EXPECT_TRUE(window.testFlag(Window::Flag::NoResize));
    EXPECT_TRUE(window.testFlag(Window::Flag::NoMove));
    EXPECT_TRUE(window.testFlag(Window::Flag::NoTitleBar));
    EXPECT_TRUE(window.testFlag(Window::Flag::NoScrollbar));
    EXPECT_TRUE(window.testFlag(Window::Flag::NoScrollWithMouse));
}

TEST_F(WindowFixture, customFlags)
{
    Window window("Settings");

    window.setFlag(Window::Flag::AlwaysAutoResize, true);
    window.setFlag(Window::Flag::NoSavedSettings, true);
    EXPECT_TRUE(window.testFlag(Window::Flag::AlwaysAutoResize));
    EXPECT_TRUE(window.testFlag(Window::Flag::NoSavedSettings));

    window.setFlag(Window::Flag::AlwaysAutoResize, false);
    EXPECT_FALSE(window.testFlag(Window::Flag::AlwaysAutoResize));
    EXPECT_TRUE(window.testFlag(Window::Flag::NoSavedSettings));
}

TEST_F(WindowFixture, openCloseAndSignal)
{
    Window window("Settings");
    int closedCount = 0;
    window.closed.connect([&]() { ++closedCount; });

    window.close();
    EXPECT_FALSE(window.isOpen());
    EXPECT_EQ(closedCount, 1);

    window.close();
    EXPECT_EQ(closedCount, 1);

    window.open();
    EXPECT_TRUE(window.isOpen());

    window.close();
    EXPECT_EQ(closedCount, 2);
}

TEST_F(WindowFixture, popupAndModalModes)
{
    Window window("Dialog");

    window.setMode(Window::Mode::Popup);
    EXPECT_EQ(window.mode(), Window::Mode::Popup);

    window.setMode(Window::Mode::Modal);
    EXPECT_EQ(window.mode(), Window::Mode::Modal);

    window.close();
    EXPECT_FALSE(window.isOpen());
    window.open();
    EXPECT_TRUE(window.isOpen());
}

TEST_F(WindowFixture, alwaysOnTopConfiguration)
{
    Window window("Overlay");

    window.setAlwaysOnTop(true);
    EXPECT_TRUE(window.isAlwaysOnTop());

    window.setAlwaysOnTop(false);
    EXPECT_FALSE(window.isAlwaysOnTop());
}
