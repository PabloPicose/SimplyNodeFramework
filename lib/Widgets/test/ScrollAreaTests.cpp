#include <gtest/gtest.h>

#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/TableView.h"
#include "SNFWidgets/ScrollArea.h"

#include <SNFCore/Application.h>
#include <SNFCore/AbstractTableModel.h>

#include "imgui.h"

#include <algorithm>

using namespace snf::widgets;

namespace {

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

class TwentyRowTableModel final : public snf::AbstractTableModel
{
public:
    int rowCount() const override { return 20; }
    int columnCount() const override { return 6; }

    std::string data(int row, int column) const override
    {
        switch (column) {
        case 0: return "B-" + std::to_string(row + 1);
        case 1: return "Long row label " + std::to_string(row + 1);
        case 2: return std::to_string((row + 1) * 7);
        case 3: return row % 3 == 0 ? "Queued" : "Ready";
        case 4: return "This column makes horizontal scrolling visible";
        case 5: return "Extra data " + std::to_string(1000 + row);
        default: return {};
        }
    }

    std::string headerData(int section) const override
    {
        switch (section) {
        case 0: return "Id";
        case 1: return "Name";
        case 2: return "Value";
        case 3: return "State";
        case 4: return "Description";
        case 5: return "Extra";
        default: return {};
        }
    }
};

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
    float lastScrollMaxX() const { return m_lastScrollMaxX; }
    float lastScrollMaxY() const { return m_lastScrollMaxY; }
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
        m_lastScrollMaxX = ImGui::GetScrollMaxX();
        m_lastScrollMaxY = ImGui::GetScrollMaxY();
    }

private:
    float m_hintWidth = 0.0f;
    float m_hintHeight = 0.0f;
    float m_lastWidth = 0.0f;
    float m_lastHeight = 0.0f;
    float m_lastScrollMaxX = 0.0f;
    float m_lastScrollMaxY = 0.0f;
    int m_renderCount = 0;
};

class RecordingTableView final : public TableView
{
public:
    using TableView::TableView;

    float lastScrollMaxX() const { return m_lastScrollMaxX; }
    float lastScrollMaxY() const { return m_lastScrollMaxY; }

protected:
    void renderImGuiConstrained(float width, float height) override
    {
        TableView::renderImGuiConstrained(width, height);
        m_lastScrollMaxX = ImGui::GetScrollMaxX();
        m_lastScrollMaxY = ImGui::GetScrollMaxY();
    }

private:
    float m_lastScrollMaxX = 0.0f;
    float m_lastScrollMaxY = 0.0f;
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

TEST_F(ScrollAreaFixture, sizeHintDoesNotForceContentSize)
{
    ScrollArea area;
    RecordingContent content(240.0f, 320.0f);

    area.setWidget(&content);

    const Size hint = area.sizeHint();
    EXPECT_FLOAT_EQ(hint.width, 0.0f);
    EXPECT_FLOAT_EQ(hint.height, 0.0f);
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

TEST_F(ScrollAreaFixture, horizontalScrollAppearsWithoutArtificialVerticalScroll)
{
    test::ImGuiInteractionHarness harness;
    TestScrollArea area;
    RecordingContent content(240.0f, 40.0f);

    area.setWidgetResizable(true);
    area.setWidget(&content);
    renderScrollArea(harness, area, 120.0f, 100.0f);
    renderScrollArea(harness, area, 120.0f, 100.0f);

    EXPECT_GT(content.lastScrollMaxX(), 0.0f);
    EXPECT_FLOAT_EQ(content.lastScrollMaxY(), 0.0f);
}

TEST_F(ScrollAreaFixture, verticalScrollAppearsOnlyWhenContentIsTallerThanViewport)
{
    test::ImGuiInteractionHarness harness;
    TestScrollArea area;
    RecordingContent content(40.0f, 240.0f);

    area.setWidgetResizable(true);
    area.setWidget(&content);
    renderScrollArea(harness, area, 120.0f, 100.0f);
    renderScrollArea(harness, area, 120.0f, 100.0f);

    EXPECT_FLOAT_EQ(content.lastScrollMaxX(), 0.0f);
    EXPECT_GT(content.lastScrollMaxY(), 0.0f);
}

TEST_F(ScrollAreaFixture, noScrollAppearsWhenResizableContentFitsViewport)
{
    test::ImGuiInteractionHarness harness;
    TestScrollArea area;
    RecordingContent content(40.0f, 40.0f);

    area.setWidgetResizable(true);
    area.setWidget(&content);
    renderScrollArea(harness, area, 120.0f, 100.0f);
    renderScrollArea(harness, area, 120.0f, 100.0f);

    EXPECT_FLOAT_EQ(content.lastScrollMaxX(), 0.0f);
    EXPECT_FLOAT_EQ(content.lastScrollMaxY(), 0.0f);
}

TEST_F(ScrollAreaFixture, tableViewContentProducesHorizontalScroll)
{
    test::ImGuiInteractionHarness harness;
    TestScrollArea area;
    WideTableModel model;
    RecordingTableView table;

    table.setModel(&model);
    area.setWidgetResizable(true);
    area.setWidget(&table);
    renderScrollArea(harness, area, 180.0f, 220.0f);
    renderScrollArea(harness, area, 180.0f, 220.0f);

    EXPECT_GT(table.lastScrollMaxX(), 0.0f);
    EXPECT_FLOAT_EQ(table.lastScrollMaxY(), 0.0f);
}

TEST_F(ScrollAreaFixture, twentyRowTableFitsWithoutVerticalScrollWhenViewportIsTallEnough)
{
    test::ImGuiInteractionHarness harness;
    TestScrollArea area;
    TwentyRowTableModel model;
    RecordingTableView table;

    table.setModel(&model);
    table.setStretchLastColumn(true);
    area.setWidgetResizable(true);
    area.setWidget(&table);
    renderScrollArea(harness, area, 1080.0f, 520.0f);
    renderScrollArea(harness, area, 1080.0f, 520.0f);

    EXPECT_FLOAT_EQ(table.lastScrollMaxX(), 0.0f);
    EXPECT_FLOAT_EQ(table.lastScrollMaxY(), 0.0f);
}
