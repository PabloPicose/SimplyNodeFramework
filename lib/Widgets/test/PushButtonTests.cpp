#include <gtest/gtest.h>

#include "SNFWidgets/PushButton.h"

#include <SNFCore/Application.h>

#include "imgui.h"

using namespace snf::widgets;

namespace {

struct TestRect {
    ImVec2 min;
    ImVec2 max;
    bool hovered = false;
    bool active = false;
};

class TestPushButton final : public PushButton
{
public:
    using PushButton::PushButton;

    void renderForTest()
    {
        renderWidget();
    }
};

class ImGuiInteractionHarness
{
public:
    ImGuiInteractionHarness()
    {
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);

        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    ~ImGuiInteractionHarness()
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::DestroyContext(m_context);
    }

    TestRect render(TestPushButton& button, const ImVec2& mousePos, bool mouseDown)
    {
        ImGui::SetCurrentContext(m_context);

        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = mousePos;
        io.MouseDown[0] = mouseDown;
        io.AddMousePosEvent(mousePos.x, mousePos.y);
        io.AddMouseButtonEvent(0, mouseDown);

        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
        ImGui::Begin("InteractionTest",
                     nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos(ImVec2(32.0f, 32.0f));

        button.renderForTest();
        const TestRect rect{
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::IsItemHovered(),
            ImGui::IsItemActive()};

        ImGui::End();
        ImGui::Render();

        return rect;
    }

private:
    ImGuiContext* m_context = nullptr;
};

ImVec2 centerOf(const TestRect& rect)
{
    return ImVec2((rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f);
}

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
    ImGuiInteractionHarness harness;
    TestPushButton button("Connect");
    int clickedCount = 0;
    button.clicked.connect([&]() { ++clickedCount; });

    // ImGui intentionally ignores mouse clicks during the first frames after
    // focus/context startup. Warm up the logical frame state before clicking.
    harness.render(button, ImVec2(-100.0f, -100.0f), false);
    harness.render(button, ImVec2(-100.0f, -100.0f), false);
    const TestRect rect = harness.render(button, ImVec2(-100.0f, -100.0f), false);
    const ImVec2 center = centerOf(rect);

    const TestRect downRect = harness.render(button, center, true);
    EXPECT_TRUE(downRect.hovered);
    EXPECT_TRUE(downRect.active);
    EXPECT_EQ(clickedCount, 0);

    const TestRect upRect = harness.render(button, center, false);
    EXPECT_TRUE(upRect.hovered);

    EXPECT_EQ(clickedCount, 1);
}

TEST_F(PushButtonFixture, disabledButtonIgnoresMouseClick)
{
    ImGuiInteractionHarness harness;
    TestPushButton button("Connect");
    int clickedCount = 0;
    button.clicked.connect([&]() { ++clickedCount; });

    harness.render(button, ImVec2(-100.0f, -100.0f), false);
    harness.render(button, ImVec2(-100.0f, -100.0f), false);
    const TestRect rect = harness.render(button, ImVec2(-100.0f, -100.0f), false);
    const ImVec2 center = centerOf(rect);

    harness.render(button, center, true);
    harness.render(button, center, false);
    EXPECT_EQ(clickedCount, 1);

    button.setDisabled(true);

    harness.render(button, center, true);
    harness.render(button, center, false);

    EXPECT_EQ(clickedCount, 1);
}
