#include <gtest/gtest.h>

#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/PushButton.h"

#include <SNFCore/Application.h>

using namespace snf::widgets;

namespace {

class PushButtonFixture : public ::testing::Test
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

}  // namespace

TEST_F(PushButtonFixture, labelCanBeChanged)
{
    PushButton button("Run");

    EXPECT_EQ(button.label(), "Run");

    button.setLabel("Stop");

    EXPECT_EQ(button.label(), "Stop");
}

TEST_F(PushButtonFixture, mouseClickEmitsClickedSignal)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<PushButton> button("Connect");
    int clickedCount = 0;
    button.clicked.connect([&]() { ++clickedCount; });

    // ImGui intentionally ignores mouse clicks during the first frames after
    // focus/context startup. Warm up the logical frame state before clicking.
    harness.beginFrame();
    harness.render(button);
    harness.endFrame();
    harness.beginFrame();
    harness.render(button);
    harness.endFrame();
    harness.beginFrame();
    const test::InteractionRect rect = harness.render(button);
    harness.endFrame();

    const ImVec2 center = test::ImGuiInteractionHarness::centerOf(rect);

    harness.beginFrame(center, true);
    const test::InteractionRect downRect = harness.render(button);
    harness.endFrame();
    EXPECT_TRUE(downRect.hovered);
    EXPECT_TRUE(downRect.active);
    EXPECT_EQ(clickedCount, 0);

    harness.beginFrame(center, false);
    const test::InteractionRect upRect = harness.render(button);
    harness.endFrame();
    EXPECT_TRUE(upRect.hovered);

    EXPECT_EQ(clickedCount, 1);
}

TEST_F(PushButtonFixture, disabledButtonIgnoresMouseClick)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<PushButton> button("Connect");
    int clickedCount = 0;
    button.clicked.connect([&]() { ++clickedCount; });

    harness.beginFrame();
    harness.render(button);
    harness.endFrame();
    harness.beginFrame();
    harness.render(button);
    harness.endFrame();
    harness.beginFrame();
    const test::InteractionRect rect = harness.render(button);
    harness.endFrame();

    const ImVec2 center = test::ImGuiInteractionHarness::centerOf(rect);

    harness.beginFrame(center, true);
    harness.render(button);
    harness.endFrame();
    harness.beginFrame(center, false);
    harness.render(button);
    harness.endFrame();
    EXPECT_EQ(clickedCount, 1);

    button.setDisabled(true);

    harness.beginFrame(center, true);
    harness.render(button);
    harness.endFrame();
    harness.beginFrame(center, false);
    harness.render(button);
    harness.endFrame();

    EXPECT_EQ(clickedCount, 1);
}
