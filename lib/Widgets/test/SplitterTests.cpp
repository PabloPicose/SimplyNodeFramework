#include <gtest/gtest.h>

#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/Layout.h"
#include "SNFWidgets/PushButton.h"
#include "SNFWidgets/ScrollArea.h"
#include "SNFWidgets/Splitter.h"
#include "SNFWidgets/TableView.h"

#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Application.h>

#include "imgui.h"

#include <algorithm>
#include <iostream>

using namespace snf::widgets;

namespace {

class TestSplitter final : public Splitter
{
public:
    using Splitter::Splitter;

    void renderConstrainedForTest(float width, float height)
    {
        renderImGuiConstrained(width, height);
    }
};

class RecordingPane final : public Widget
{
public:
    using Widget::Widget;

    float lastWidth() const { return m_lastWidth; }
    float lastHeight() const { return m_lastHeight; }
    int renderCount() const { return m_renderCount; }

    Size sizeHint() const override { return Size{m_hintWidth, m_hintHeight}; }

    void setSizeHint(float width, float height)
    {
        m_hintWidth = std::max(0.0f, width);
        m_hintHeight = std::max(0.0f, height);
    }

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
    float m_lastWidth = 0.0f;
    float m_lastHeight = 0.0f;
    float m_hintWidth = 50.0f;
    float m_hintHeight = 24.0f;
    int m_renderCount = 0;
};

class WideTableModel final : public snf::AbstractTableModel
{
public:
    int rowCount() const override { return 4; }
    int columnCount() const override { return 6; }

    std::string data(int row, int column) const override
    {
        return "R" + std::to_string(row + 1) + " wide column " + std::to_string(column + 1);
    }

    std::string headerData(int section) const override
    {
        return "Column " + std::to_string(section + 1);
    }
};

class RecordingTableView final : public TableView
{
public:
    using TableView::TableView;

    float lastWidth() const { return m_lastWidth; }
    float lastHeight() const { return m_lastHeight; }
    float lastScrollMaxX() const { return m_lastScrollMaxX; }
    float lastScrollMaxY() const { return m_lastScrollMaxY; }

protected:
    void renderImGuiConstrained(float width, float height) override
    {
        m_lastWidth = width;
        m_lastHeight = height;
        TableView::renderImGuiConstrained(width, height);
        m_lastScrollMaxX = ImGui::GetScrollMaxX();
        m_lastScrollMaxY = ImGui::GetScrollMaxY();
    }

private:
    float m_lastWidth = 0.0f;
    float m_lastHeight = 0.0f;
    float m_lastScrollMaxX = 0.0f;
    float m_lastScrollMaxY = 0.0f;
};

void renderSplitter(test::ImGuiInteractionHarness& harness, TestSplitter& splitter, float width, float height)
{
    harness.beginFrame();
    splitter.renderConstrainedForTest(width, height);
    harness.endFrame();
}

void renderSplitterAt(
    test::ImGuiInteractionHarness& harness,
    TestSplitter& splitter,
    float width,
    float height,
    const ImVec2& mousePos,
    bool mouseDown)
{
    harness.beginFrame(mousePos, mouseDown);
    splitter.renderConstrainedForTest(width, height);
    harness.endFrame();
}

}  // namespace

class SplitterFixture : public ::testing::Test
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

TEST_F(SplitterFixture, defaultState)
{
    Splitter splitter;

    EXPECT_EQ(splitter.orientation(), Splitter::Orientation::Horizontal);
    EXPECT_FLOAT_EQ(splitter.ratio(), 0.5f);
    EXPECT_FLOAT_EQ(splitter.handleSize(), 8.0f);
    EXPECT_EQ(splitter.primaryWidget(), nullptr);
    EXPECT_EQ(splitter.secondaryWidget(), nullptr);
}

TEST_F(SplitterFixture, widgetsAreAssignedAndReparented)
{
    Splitter splitter;
    RecordingPane primary;
    RecordingPane secondary;

    splitter.setPrimaryWidget(&primary);
    splitter.setSecondaryWidget(&secondary);

    EXPECT_EQ(splitter.primaryWidget(), &primary);
    EXPECT_EQ(splitter.secondaryWidget(), &secondary);
    EXPECT_EQ(splitter.widget(0), &primary);
    EXPECT_EQ(splitter.widget(1), &secondary);
    EXPECT_EQ(primary.parent(), &splitter);
    EXPECT_EQ(secondary.parent(), &splitter);
}

TEST_F(SplitterFixture, setWidgetSupportsBothPanes)
{
    Splitter splitter;
    RecordingPane primary;
    RecordingPane secondary;

    splitter.setWidget(0, &primary);
    splitter.setWidget(1, &secondary);
    splitter.setWidget(2, nullptr);

    EXPECT_EQ(splitter.primaryWidget(), &primary);
    EXPECT_EQ(splitter.secondaryWidget(), &secondary);
    EXPECT_EQ(splitter.widget(2), nullptr);
}

TEST_F(SplitterFixture, ratioAndSizesAreClamped)
{
    Splitter splitter;

    splitter.setRatio(1.5f);
    EXPECT_FLOAT_EQ(splitter.ratio(), 1.0f);

    splitter.setRatio(-1.0f);
    EXPECT_FLOAT_EQ(splitter.ratio(), 0.0f);

    splitter.setSizes(100.0f, 300.0f);
    EXPECT_FLOAT_EQ(splitter.ratio(), 0.25f);
    EXPECT_FLOAT_EQ(splitter.primarySize(), 100.0f);
    EXPECT_FLOAT_EQ(splitter.secondarySize(), 300.0f);

    splitter.setInitialRatio(0.75f);
    EXPECT_FLOAT_EQ(splitter.ratio(), 0.75f);

    splitter.setInitialSizes(300.0f, 100.0f);
    EXPECT_FLOAT_EQ(splitter.ratio(), 0.75f);
}

TEST_F(SplitterFixture, horizontalSplitterDistributesWidth)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter splitter;
    RecordingPane primary;
    RecordingPane secondary;

    splitter.setPrimaryWidget(&primary);
    splitter.setSecondaryWidget(&secondary);
    splitter.setRatio(0.25f);

    renderSplitter(harness, splitter, 408.0f, 120.0f);

    EXPECT_NEAR(primary.lastWidth(), 100.0f, 1.0f);
    EXPECT_NEAR(secondary.lastWidth(), 300.0f, 1.0f);
    EXPECT_NEAR(primary.lastHeight(), 120.0f, 1.0f);
    EXPECT_NEAR(secondary.lastHeight(), 120.0f, 1.0f);
}

TEST_F(SplitterFixture, verticalSplitterDistributesHeight)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter splitter(Splitter::Orientation::Vertical);
    RecordingPane primary;
    RecordingPane secondary;

    splitter.setPrimaryWidget(&primary);
    splitter.setSecondaryWidget(&secondary);
    splitter.setRatio(0.25f);

    renderSplitter(harness, splitter, 160.0f, 408.0f);

    EXPECT_NEAR(primary.lastHeight(), 100.0f, 1.0f);
    EXPECT_NEAR(secondary.lastHeight(), 300.0f, 1.0f);
    EXPECT_NEAR(primary.lastWidth(), 160.0f, 1.0f);
    EXPECT_NEAR(secondary.lastWidth(), 160.0f, 1.0f);
}

TEST_F(SplitterFixture, minimumSizesClampPaneGeometry)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter splitter;
    RecordingPane primary;
    RecordingPane secondary;

    splitter.setPrimaryWidget(&primary);
    splitter.setSecondaryWidget(&secondary);
    splitter.setMinimumSizes(120.0f, 80.0f);
    splitter.setRatio(0.05f);

    renderSplitter(harness, splitter, 408.0f, 120.0f);

    EXPECT_NEAR(primary.lastWidth(), 120.0f, 1.0f);
    EXPECT_NEAR(secondary.lastWidth(), 280.0f, 1.0f);
}

TEST_F(SplitterFixture, impossibleMinimumSizesStillAvoidNegativeGeometry)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter splitter;
    RecordingPane primary;
    RecordingPane secondary;

    splitter.setPrimaryWidget(&primary);
    splitter.setSecondaryWidget(&secondary);
    splitter.setMinimumSizes(300.0f, 300.0f);
    splitter.setRatio(0.5f);

    renderSplitter(harness, splitter, 208.0f, 120.0f);

    EXPECT_GE(primary.lastWidth(), 0.0f);
    EXPECT_GE(secondary.lastWidth(), 0.0f);
    EXPECT_NEAR(primary.lastWidth() + secondary.lastWidth(), 200.0f, 1.0f);
}

TEST_F(SplitterFixture, nestedScrollAreaStillShowsHorizontalScrollWhenPaneNarrows)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter splitter;
    RecordingPane leftPane;
    VBoxLayout rightPane;
    ScrollArea area;
    WideTableModel model;
    RecordingTableView table;

    splitter.setPrimaryWidget(&leftPane);
    splitter.setSecondaryWidget(&rightPane);
    splitter.setMinimumSizes(80.0f, 80.0f);
    splitter.setRatio(0.85f);

    area.setWidgetResizable(true);
    area.setHorizontalScrollBarPolicy(ScrollArea::ScrollBarPolicy::AsNeeded);
    area.setVerticalScrollBarPolicy(ScrollArea::ScrollBarPolicy::AsNeeded);
    rightPane.addWidget(&area, 1);

    table.setModel(&model);
    table.setStretchLastColumn(true);
    area.setWidget(&table);

    renderSplitter(harness, splitter, 420.0f, 220.0f);
    renderSplitter(harness, splitter, 420.0f, 220.0f);

    EXPECT_GT(table.lastScrollMaxX(), 0.0f);
    EXPECT_FLOAT_EQ(table.lastScrollMaxY(), 0.0f);
}

TEST_F(SplitterFixture, draggingHorizontalHandleUpdatesRatio)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter splitter;
    RecordingPane primary;
    RecordingPane secondary;

    splitter.setPrimaryWidget(&primary);
    splitter.setSecondaryWidget(&secondary);
    splitter.setRatio(0.5f);

    renderSplitter(harness, splitter, 408.0f, 120.0f);

    const ImVec2 handleCenter(32.0f + 200.0f + 4.0f, 32.0f + 60.0f);
    renderSplitterAt(harness, splitter, 408.0f, 120.0f, handleCenter, true);
    renderSplitterAt(harness, splitter, 408.0f, 120.0f, ImVec2(handleCenter.x + 40.0f, handleCenter.y), true);
    renderSplitterAt(harness, splitter, 408.0f, 120.0f, ImVec2(handleCenter.x + 40.0f, handleCenter.y), false);

    EXPECT_GT(splitter.ratio(), 0.55f);
}

TEST_F(SplitterFixture, canContainLayoutsAndNestedSplitters)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter outer;
    auto* inner = new Splitter(Splitter::Orientation::Vertical);
    auto* layout = new VBoxLayout;
    auto* button = new PushButton("Apply");
    auto* top = new RecordingPane;
    auto* bottom = new RecordingPane;

    layout->addWidget(button);
    inner->setPrimaryWidget(top);
    inner->setSecondaryWidget(bottom);
    outer.setPrimaryWidget(layout);
    outer.setSecondaryWidget(inner);

    renderSplitter(harness, outer, 500.0f, 240.0f);

    EXPECT_EQ(layout->parent(), &outer);
    EXPECT_EQ(inner->parent(), &outer);
    EXPECT_EQ(top->parent(), inner);
    EXPECT_EQ(bottom->parent(), inner);
    EXPECT_GT(top->renderCount(), 0);
    EXPECT_GT(bottom->renderCount(), 0);
}

TEST_F(SplitterFixture, sizeHintCombinesPaneHintsAndHandle)
{
    test::ImGuiInteractionHarness harness;
    TestSplitter splitter;
    RecordingPane primary;
    RecordingPane secondary;

    primary.setSizeHint(100.0f, 20.0f);
    secondary.setSizeHint(50.0f, 40.0f);
    splitter.setPrimaryWidget(&primary);
    splitter.setSecondaryWidget(&secondary);
    splitter.setHandleSize(10.0f);

    harness.beginFrame();
    const Size horizontal = splitter.sizeHint();
    splitter.setOrientation(Splitter::Orientation::Vertical);
    const Size vertical = splitter.sizeHint();
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    harness.endFrame();

    EXPECT_NEAR(horizontal.width, 160.0f, 1.0f);
    EXPECT_NEAR(horizontal.height, 40.0f, 1.0f);
    EXPECT_NEAR(vertical.width, 100.0f, 1.0f);
    EXPECT_NEAR(vertical.height, 70.0f, 1.0f);
}
