#include <gtest/gtest.h>

#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/Label.h"
#include "SNFWidgets/Layout.h"
#include "SNFWidgets/PushButton.h"
#include "SNFWidgets/Tabs.h"

#include <SNFCore/Application.h>

#include "imgui.h"

using namespace snf::widgets;

namespace {

class TestTabs final : public Tabs
{
public:
    using Tabs::Tabs;

    void renderConstrainedForTest(float width, float height)
    {
        renderImGuiConstrained(width, height);
    }
};

class RecordingPage final : public Widget
{
public:
    explicit RecordingPage(float hintWidth = 0.0f, float hintHeight = 0.0f, snf::Node* parent = nullptr)
        : Widget(parent), m_hintWidth(hintWidth), m_hintHeight(hintHeight)
    {
    }

    Size sizeHint() const override
    {
        return Size{m_hintWidth, m_hintHeight};
    }

    int renderCount() const { return m_renderCount; }
    float lastWidth() const { return m_lastWidth; }
    float lastHeight() const { return m_lastHeight; }

protected:
    void renderImGui() override
    {
        renderImGuiConstrained(-1.0f, -1.0f);
    }

    void renderImGuiConstrained(float width, float height) override
    {
        ++m_renderCount;
        m_lastWidth = width;
        m_lastHeight = height;
        ImGui::Dummy(ImVec2(std::max(1.0f, width), std::max(1.0f, height)));
    }

private:
    float m_hintWidth = 0.0f;
    float m_hintHeight = 0.0f;
    int m_renderCount = 0;
    float m_lastWidth = 0.0f;
    float m_lastHeight = 0.0f;
};

void renderTabs(test::ImGuiInteractionHarness& harness, TestTabs& tabs, float width, float height)
{
    harness.beginFrame();
    tabs.renderConstrainedForTest(width, height);
    harness.endFrame();
}

}  // namespace

class TabsFixture : public ::testing::Test
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

TEST_F(TabsFixture, defaultState)
{
    Tabs tabs;

    EXPECT_EQ(tabs.count(), 0);
    EXPECT_EQ(tabs.currentIndex(), -1);
    EXPECT_EQ(tabs.widgetAt(0), nullptr);
    EXPECT_EQ(tabs.tabTitleAt(0), "");
}

TEST_F(TabsFixture, addTabReparentsPageAndSelectsFirstTab)
{
    Tabs tabs;
    auto* pageA = new RecordingPage();
    auto* pageB = new RecordingPage();

    const int firstIndex = tabs.addTab("General", pageA);
    const int secondIndex = tabs.addTab("Advanced", pageB);

    EXPECT_EQ(firstIndex, 0);
    EXPECT_EQ(secondIndex, 1);
    EXPECT_EQ(tabs.count(), 2);
    EXPECT_EQ(tabs.currentIndex(), 0);
    EXPECT_EQ(tabs.widgetAt(0), pageA);
    EXPECT_EQ(tabs.widgetAt(1), pageB);
    EXPECT_EQ(tabs.tabTitleAt(0), "General");
    EXPECT_EQ(tabs.tabTitleAt(1), "Advanced");
    EXPECT_EQ(pageA->parent(), &tabs);
    EXPECT_EQ(pageB->parent(), &tabs);
}

TEST_F(TabsFixture, setCurrentIndexChangesRenderedPageAndEmitsSignal)
{
    test::ImGuiInteractionHarness harness;
    TestTabs tabs;
    auto* pageA = new RecordingPage(120.0f, 80.0f);
    auto* pageB = new RecordingPage(140.0f, 90.0f);
    int currentChangedCount = 0;
    int lastCurrentIndex = -1;

    tabs.addTab("A", pageA);
    tabs.addTab("B", pageB);
    tabs.currentChanged.connect([&](int index) {
        ++currentChangedCount;
        lastCurrentIndex = index;
    });

    renderTabs(harness, tabs, 420.0f, 220.0f);
    EXPECT_EQ(pageA->renderCount(), 1);
    EXPECT_EQ(pageB->renderCount(), 0);

    tabs.setCurrentIndex(1);
    renderTabs(harness, tabs, 420.0f, 220.0f);

    EXPECT_EQ(tabs.currentIndex(), 1);
    EXPECT_EQ(currentChangedCount, 1);
    EXPECT_EQ(lastCurrentIndex, 1);
    EXPECT_GE(pageA->renderCount(), 1);
    EXPECT_EQ(pageB->renderCount(), 1);
    EXPECT_GT(pageB->lastWidth(), 0.0f);
    EXPECT_GT(pageB->lastHeight(), 0.0f);
}

TEST_F(TabsFixture, removeTabUpdatesCurrentIndex)
{
    Tabs tabs;
    auto* pageA = new RecordingPage();
    auto* pageB = new RecordingPage();
    auto* pageC = new RecordingPage();

    tabs.addTab("A", pageA);
    tabs.addTab("B", pageB);
    tabs.addTab("C", pageC);
    tabs.setCurrentIndex(2);

    tabs.removeTab(1);

    EXPECT_EQ(tabs.count(), 2);
    EXPECT_EQ(tabs.currentIndex(), 1);
    EXPECT_EQ(tabs.tabTitleAt(0), "A");
    EXPECT_EQ(tabs.tabTitleAt(1), "C");
}

TEST_F(TabsFixture, clearRemovesAllTabsAndSelection)
{
    Tabs tabs;
    auto* pageA = new RecordingPage();
    auto* pageB = new RecordingPage();

    tabs.addTab("A", pageA);
    tabs.addTab("B", pageB);
    tabs.clear();

    EXPECT_EQ(tabs.count(), 0);
    EXPECT_EQ(tabs.currentIndex(), -1);
    EXPECT_EQ(tabs.widgetAt(0), nullptr);
}

TEST_F(TabsFixture, containsWidgetFindsNestedPageWidgets)
{
    Tabs tabs;
    auto* page = new VBoxLayout();
    auto* label = new Label("Page title", page);
    auto* button = new PushButton("Apply", page);

    page->addWidget(label);
    page->addWidget(button);
    tabs.addTab("Nested", page);

    EXPECT_TRUE(tabs.containsWidget(page));
    EXPECT_TRUE(tabs.containsWidget(label));
    EXPECT_TRUE(tabs.containsWidget(button));
}

TEST_F(TabsFixture, sizeHintUsesLargestPagePlusTabBarHeight)
{
    test::ImGuiInteractionHarness harness;
    Tabs tabs;
    auto* pageA = new RecordingPage(180.0f, 90.0f);
    auto* pageB = new RecordingPage(260.0f, 140.0f);

    tabs.addTab("First", pageA);
    tabs.addTab("Second", pageB);

    harness.beginFrame();
    const float tabBarHeight = ImGui::GetFrameHeightWithSpacing();
    const Size hint = tabs.sizeHint();
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    harness.endFrame();

    EXPECT_NEAR(hint.width, 260.0f, 1.0f);
    EXPECT_NEAR(hint.height, 140.0f + tabBarHeight, 2.0f);
}
