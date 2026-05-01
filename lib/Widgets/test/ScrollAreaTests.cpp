#include <gtest/gtest.h>

#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/ScrollArea.h"

#include <SNFCore/Application.h>

#include "imgui.h"

#include <algorithm>

using namespace snf::widgets;

namespace {

class TestScrollArea final : public ScrollArea
{
public:
    using ScrollArea::ScrollArea;

    void renderConstrainedForTest(float width, float height)
    {
        renderImGuiConstrained(width, height);
    }
};

class RecordingContent final : public Widget
{
public:
    explicit RecordingContent(float hintWidth = 0.0f, float hintHeight = 0.0f, snf::Node* parent = nullptr)
        : Widget(parent), m_hintWidth(hintWidth), m_hintHeight(hintHeight)
    {
    }

    Size sizeHint() const override
    {
        return Size{m_hintWidth, m_hintHeight};
    }

    float lastWidth() const { return m_lastWidth; }
    float lastHeight() const { return m_lastHeight; }
    int renderCount() const { return m_renderCount; }

protected:
    void renderImGui() override
    {
        renderImGuiConstrained(-1.0f, -1.0f);
    }

    void renderImGuiConstrained(float width, float height) override
    {
        m_lastWidth = width;
        m_lastHeight = height;
        ++m_renderCount;
        ImGui::Dummy(ImVec2(std::max(1.0f, width), std::max(1.0f, height)));
    }

private:
    float m_hintWidth = 0.0f;
    float m_hintHeight = 0.0f;
    float m_lastWidth = 0.0f;
    float m_lastHeight = 0.0f;
    int m_renderCount = 0;
};

void renderScrollArea(test::ImGuiInteractionHarness& harness, TestScrollArea& area, float width, float height)
{
    harness.beginFrame();
    area.renderConstrainedForTest(width, height);
    harness.endFrame();
}

}  // namespace

class ScrollAreaFixture : public ::testing::Test
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

TEST_F(ScrollAreaFixture, defaultState)
{
    ScrollArea area;

    EXPECT_EQ(area.widget(), nullptr);
    EXPECT_FALSE(area.widgetResizable());
    EXPECT_EQ(area.verticalScrollBarPolicy(), ScrollArea::ScrollBarPolicy::AsNeeded);
    EXPECT_EQ(area.horizontalScrollBarPolicy(), ScrollArea::ScrollBarPolicy::AsNeeded);
}

TEST_F(ScrollAreaFixture, setWidgetReparentsContent)
{
    ScrollArea area;
    RecordingContent content;

    area.setWidget(&content);

    EXPECT_EQ(area.widget(), &content);
    EXPECT_EQ(content.parent(), &area);
}

TEST_F(ScrollAreaFixture, policiesRoundTrip)
{
    ScrollArea area;

    area.setVerticalScrollBarPolicy(ScrollArea::ScrollBarPolicy::AlwaysOn);
    area.setHorizontalScrollBarPolicy(ScrollArea::ScrollBarPolicy::AlwaysOff);

    EXPECT_EQ(area.verticalScrollBarPolicy(), ScrollArea::ScrollBarPolicy::AlwaysOn);
    EXPECT_EQ(area.horizontalScrollBarPolicy(), ScrollArea::ScrollBarPolicy::AlwaysOff);
}

TEST_F(ScrollAreaFixture, sizeHintUsesContentHint)
{
    ScrollArea area;
    RecordingContent content(240.0f, 320.0f);

    area.setWidget(&content);

    const Size hint = area.sizeHint();
    EXPECT_FLOAT_EQ(hint.width, 240.0f);
    EXPECT_FLOAT_EQ(hint.height, 320.0f);
}

TEST_F(ScrollAreaFixture, nonResizableContentKeepsContentHintInsideViewport)
{
    test::ImGuiInteractionHarness harness;
    TestScrollArea area;
    RecordingContent content(240.0f, 320.0f);

    area.setWidget(&content);
    renderScrollArea(harness, area, 120.0f, 80.0f);

    EXPECT_EQ(content.renderCount(), 1);
    EXPECT_NEAR(content.lastWidth(), 240.0f, 1.0f);
    EXPECT_NEAR(content.lastHeight(), 320.0f, 1.0f);
}

TEST_F(ScrollAreaFixture, resizableContentFillsViewportAtMinimum)
{
    test::ImGuiInteractionHarness harness;
    TestScrollArea area;
    RecordingContent content(20.0f, 20.0f);

    area.setWidgetResizable(true);
    area.setWidget(&content);
    renderScrollArea(harness, area, 120.0f, 80.0f);

    EXPECT_EQ(content.renderCount(), 1);
    EXPECT_GE(content.lastWidth(), 110.0f);
    EXPECT_GE(content.lastHeight(), 70.0f);
}
