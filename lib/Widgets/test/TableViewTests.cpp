#include <gtest/gtest.h>

#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/TableView.h"
#include <SNFCore/Application.h>
#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/ModelIndex.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

class TestTableModel : public snf::AbstractTableModel
{
public:
    TestTableModel() = default;

    TestTableModel(std::vector<std::vector<std::string>> rows, std::vector<std::string> headers)
        : m_rows(std::move(rows)), m_headers(std::move(headers))
    {
    }

    int rowCount() const override
    {
        return static_cast<int>(m_rows.size());
    }

    int columnCount() const override
    {
        return static_cast<int>(m_headers.size());
    }

    std::string data(int row, int column) const override
    {
        if (! isValidCell(row, column)) {
            return {};
        }
        return m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    }

    std::string headerData(int section) const override
    {
        if (section < 0 || section >= static_cast<int>(m_headers.size())) {
            return {};
        }
        return m_headers[static_cast<std::size_t>(section)];
    }

    bool insertRows(int row, int count) override
    {
        if (row < 0 || row > rowCount() || count <= 0) {
            return false;
        }

        m_rows.insert(m_rows.begin() + row,
                      static_cast<std::size_t>(count),
                      std::vector<std::string>(static_cast<std::size_t>(columnCount())));
        notifyRowsInserted(row, count);
        return true;
    }

    bool removeRows(int row, int count) override
    {
        if (row < 0 || count <= 0 || row + count > rowCount()) {
            return false;
        }

        m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
        notifyRowsRemoved(row, count);
        return true;
    }

    bool insertColumns(int column, int count) override
    {
        if (column < 0 || column > columnCount() || count <= 0) {
            return false;
        }

        m_headers.insert(m_headers.begin() + column, static_cast<std::size_t>(count), std::string());
        for (auto& row : m_rows) {
            row.insert(row.begin() + column, static_cast<std::size_t>(count), std::string());
        }
        notifyColumnsInserted(column, count);
        return true;
    }

    bool removeColumns(int column, int count) override
    {
        if (column < 0 || count <= 0 || column + count > columnCount()) {
            return false;
        }

        m_headers.erase(m_headers.begin() + column, m_headers.begin() + column + count);
        for (auto& row : m_rows) {
            row.erase(row.begin() + column, row.begin() + column + count);
        }
        notifyColumnsRemoved(column, count);
        return true;
    }

    void reset(std::vector<std::vector<std::string>> rows, std::vector<std::string> headers)
    {
        m_rows = std::move(rows);
        m_headers = std::move(headers);
        notifyModelReset();
    }

private:
    bool isValidCell(int row, int column) const
    {
        return row >= 0
            && row < static_cast<int>(m_rows.size())
            && column >= 0
            && column < static_cast<int>(m_headers.size())
            && column < static_cast<int>(m_rows[static_cast<std::size_t>(row)].size());
    }

    std::vector<std::vector<std::string>> m_rows;
    std::vector<std::string>              m_headers;
};

class TableViewFixture : public ::testing::Test
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

ImVec2 tableCellCenter(int row, int column)
{
    return ImVec2(42.0f + static_cast<float>(column) * 360.0f,
                  42.0f + static_cast<float>(row) * 22.0f);
}

template <typename TableT>
void renderWarmup(snf::widgets::test::ImGuiInteractionHarness& harness, TableT& table)
{
    harness.beginFrame();
    harness.render(table);
    harness.endFrame();
    harness.beginFrame();
    harness.render(table);
    harness.endFrame();
}

template <typename TableT>
void clickTableAt(snf::widgets::test::ImGuiInteractionHarness& harness,
                  TableT& table,
                  const ImVec2& position,
                  bool ctrlDown = false)
{
    harness.beginFrame(position, true, ctrlDown);
    harness.render(table);
    harness.endFrame();
    harness.beginFrame(position, false, ctrlDown);
    harness.render(table);
    harness.endFrame();
}

}  // namespace

TEST_F(TableViewFixture, construction)
{
    snf::widgets::TableView view;

    EXPECT_EQ(view.model(), nullptr);
    EXPECT_TRUE(view.showHorizontalHeader());
    EXPECT_TRUE(view.showGrid());
    EXPECT_TRUE(view.rowSelectionEnabled());
    EXPECT_EQ(view.selectionBehavior(), snf::widgets::TableSelectionBehavior::Rows);
    EXPECT_EQ(view.selectionMode(), snf::widgets::TableSelectionMode::Single);
    EXPECT_EQ(view.currentRow(), -1);
    EXPECT_EQ(view.currentColumn(), -1);
    EXPECT_FALSE(view.currentIndex().isValid());
    EXPECT_TRUE(view.selectedRows().empty());
    EXPECT_TRUE(view.selectedColumns().empty());
    EXPECT_TRUE(view.selectedIndexes().empty());
}

TEST_F(TableViewFixture, setModelAndClearModel)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada"}}, {"Name"});
    int changedCount = 0;
    snf::AbstractTableModel* lastModel = reinterpret_cast<snf::AbstractTableModel*>(0x1);

    view.modelChanged.connect([&](snf::AbstractTableModel* modelPtr) {
        ++changedCount;
        lastModel = modelPtr;
    });

    view.setModel(&model);
    EXPECT_EQ(view.model(), &model);
    EXPECT_EQ(changedCount, 1);
    EXPECT_EQ(lastModel, &model);

    view.setModel(&model);
    EXPECT_EQ(changedCount, 1);

    view.clearModel();
    EXPECT_EQ(view.model(), nullptr);
    EXPECT_EQ(changedCount, 2);
    EXPECT_EQ(lastModel, nullptr);
}

TEST_F(TableViewFixture, nullAndEmptyModelsRejectSelection)
{
    snf::widgets::TableView view;
    TestTableModel emptyModel;

    view.setCurrentRow(0);
    EXPECT_EQ(view.currentRow(), -1);
    EXPECT_EQ(view.currentColumn(), -1);

    view.setModel(&emptyModel);
    view.setCurrentCell(0, 0);
    EXPECT_EQ(view.currentRow(), -1);
    EXPECT_EQ(view.currentColumn(), -1);
}

TEST_F(TableViewFixture, validRowSelectionEmitsSignals)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});
    int rowSignalCount = 0;
    int cellSignalCount = 0;

    view.setModel(&model);
    view.currentRowChanged.connect([&](int row) {
        ++rowSignalCount;
        EXPECT_EQ(row, 1);
    });
    view.currentCellChanged.connect([&](int row, int column) {
        ++cellSignalCount;
        EXPECT_EQ(row, 1);
        EXPECT_EQ(column, -1);
    });

    view.setCurrentRow(1);

    EXPECT_EQ(view.currentRow(), 1);
    EXPECT_EQ(view.currentColumn(), -1);
    EXPECT_EQ(rowSignalCount, 1);
    EXPECT_EQ(cellSignalCount, 1);
}

TEST_F(TableViewFixture, duplicateRowSelectionDoesNotEmit)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada"}}, {"Name"});
    int rowSignalCount = 0;
    int cellSignalCount = 0;

    view.setModel(&model);
    view.setCurrentRow(0);
    view.currentRowChanged.connect([&](int) { ++rowSignalCount; });
    view.currentCellChanged.connect([&](int, int) { ++cellSignalCount; });

    view.setCurrentRow(0);

    EXPECT_EQ(rowSignalCount, 0);
    EXPECT_EQ(cellSignalCount, 0);
}

TEST_F(TableViewFixture, invalidRowClearsSelection)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada"}}, {"Name"});
    int rowSignalCount = 0;
    int cellSignalCount = 0;

    view.setModel(&model);
    view.setCurrentRow(0);
    view.currentRowChanged.connect([&](int row) {
        ++rowSignalCount;
        EXPECT_EQ(row, -1);
    });
    view.currentCellChanged.connect([&](int row, int column) {
        ++cellSignalCount;
        EXPECT_EQ(row, -1);
        EXPECT_EQ(column, -1);
    });

    view.setCurrentRow(8);

    EXPECT_EQ(view.currentRow(), -1);
    EXPECT_EQ(view.currentColumn(), -1);
    EXPECT_EQ(rowSignalCount, 1);
    EXPECT_EQ(cellSignalCount, 1);
}

TEST_F(TableViewFixture, validCellSelectionEmitsSignals)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    int rowSignalCount = 0;
    int columnSignalCount = 0;
    int cellSignalCount = 0;
    int indexSignalCount = 0;
    snf::ModelIndex changedIndex;

    view.setModel(&model);
    view.currentRowChanged.connect([&](int row) {
        ++rowSignalCount;
        EXPECT_EQ(row, 0);
    });
    view.currentColumnChanged.connect([&](int column) {
        ++columnSignalCount;
        EXPECT_EQ(column, 1);
    });
    view.currentCellChanged.connect([&](int row, int column) {
        ++cellSignalCount;
        EXPECT_EQ(row, 0);
        EXPECT_EQ(column, 1);
    });
    view.currentIndexChanged.connect([&](const snf::ModelIndex& index) {
        ++indexSignalCount;
        changedIndex = index;
    });

    view.setCurrentCell(0, 1);

    EXPECT_EQ(view.currentRow(), 0);
    EXPECT_EQ(view.currentColumn(), 1);
    EXPECT_EQ(view.currentIndex(), model.index(0, 1));
    EXPECT_EQ(rowSignalCount, 1);
    EXPECT_EQ(columnSignalCount, 1);
    EXPECT_EQ(cellSignalCount, 1);
    EXPECT_EQ(indexSignalCount, 1);
    EXPECT_EQ(changedIndex, model.index(0, 1));
}

TEST_F(TableViewFixture, invalidCellClearsSelection)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});

    view.setModel(&model);
    view.setCurrentCell(0, 1);
    view.setCurrentCell(0, 9);

    EXPECT_EQ(view.currentRow(), -1);
    EXPECT_EQ(view.currentColumn(), -1);
}

TEST_F(TableViewFixture, modelResetValidatesSelection)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});
    int rowSignalCount = 0;
    int cellSignalCount = 0;

    view.setModel(&model);
    view.setCurrentCell(1, 1);
    view.currentRowChanged.connect([&](int row) {
        ++rowSignalCount;
        EXPECT_EQ(row, -1);
    });
    view.currentCellChanged.connect([&](int row, int column) {
        ++cellSignalCount;
        EXPECT_EQ(row, -1);
        EXPECT_EQ(column, -1);
    });

    model.reset({{"Ada"}}, {"Name"});

    EXPECT_EQ(view.currentRow(), -1);
    EXPECT_EQ(view.currentColumn(), -1);
    EXPECT_EQ(rowSignalCount, 1);
    EXPECT_EQ(cellSignalCount, 1);
}

TEST_F(TableViewFixture, rowInsertionShiftsCurrentIndexAndSelection)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});
    int indexSignalCount = 0;
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.selectCell(1, 1);
    view.currentIndexChanged.connect([&](const snf::ModelIndex&) { ++indexSignalCount; });
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    ASSERT_TRUE(model.insertRows(0, 1));

    EXPECT_EQ(view.currentIndex(), model.index(2, 1));
    EXPECT_EQ(view.selectedIndexes(), std::vector<snf::ModelIndex>({model.index(2, 1)}));
    EXPECT_EQ(model.data(2, 1), "Hopper");
    EXPECT_EQ(indexSignalCount, 1);
    EXPECT_EQ(selectionSignalCount, 1);
}

TEST_F(TableViewFixture, rowRemovalClearsSelectionInsideRemovedRange)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada"}, {"Grace"}, {"Linus"}}, {"Name"});
    int indexSignalCount = 0;
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.selectCell(1, 0);
    view.currentIndexChanged.connect([&](const snf::ModelIndex&) { ++indexSignalCount; });
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    ASSERT_TRUE(model.removeRows(1, 1));

    EXPECT_FALSE(view.currentIndex().isValid());
    EXPECT_TRUE(view.selectedIndexes().empty());
    EXPECT_EQ(model.data(1, 0), "Linus");
    EXPECT_EQ(indexSignalCount, 1);
    EXPECT_EQ(selectionSignalCount, 1);
}

TEST_F(TableViewFixture, rowRemovalShiftsSelectionAfterRemovedRange)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada"}, {"Grace"}, {"Linus"}}, {"Name"});

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.selectCell(2, 0);

    ASSERT_TRUE(model.removeRows(0, 1));

    EXPECT_EQ(view.currentIndex(), model.index(1, 0));
    EXPECT_EQ(view.selectedIndexes(), std::vector<snf::ModelIndex>({model.index(1, 0)}));
    EXPECT_EQ(model.data(1, 0), "Linus");
}

TEST_F(TableViewFixture, columnSelectionExpandsWhenRowsAreInserted)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada"}, {"Grace"}}, {"Name"});
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Columns);
    view.selectColumn(0);
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    ASSERT_TRUE(model.insertRows(1, 1));

    EXPECT_EQ(view.selectedRows(), std::vector<int>({0, 1, 2}));
    ASSERT_EQ(view.selectedIndexes().size(), 3u);
    EXPECT_EQ(view.selectedIndexes()[0], model.index(0, 0));
    EXPECT_EQ(view.selectedIndexes()[1], model.index(1, 0));
    EXPECT_EQ(view.selectedIndexes()[2], model.index(2, 0));
    EXPECT_EQ(selectionSignalCount, 1);
}

TEST_F(TableViewFixture, columnInsertionShiftsCurrentIndexAndSelection)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    int indexSignalCount = 0;
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.selectCell(0, 1);
    view.currentIndexChanged.connect([&](const snf::ModelIndex&) { ++indexSignalCount; });
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    ASSERT_TRUE(model.insertColumns(1, 1));

    EXPECT_EQ(view.currentIndex(), model.index(0, 2));
    EXPECT_EQ(view.selectedIndexes(), std::vector<snf::ModelIndex>({model.index(0, 2)}));
    EXPECT_EQ(model.data(0, 2), "Lovelace");
    EXPECT_EQ(indexSignalCount, 1);
    EXPECT_EQ(selectionSignalCount, 1);
}

TEST_F(TableViewFixture, rowSelectionExpandsWhenColumnsAreInserted)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Rows);
    view.selectRow(0);
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    ASSERT_TRUE(model.insertColumns(1, 1));

    EXPECT_EQ(view.selectedColumns(), std::vector<int>({0, 1, 2}));
    ASSERT_EQ(view.selectedIndexes().size(), 3u);
    EXPECT_EQ(view.selectedIndexes()[0], model.index(0, 0));
    EXPECT_EQ(view.selectedIndexes()[1], model.index(0, 1));
    EXPECT_EQ(view.selectedIndexes()[2], model.index(0, 2));
    EXPECT_EQ(selectionSignalCount, 1);
}

TEST_F(TableViewFixture, columnRemovalClearsSelectionInsideRemovedRange)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Augusta", "Lovelace"}}, {"First", "Middle", "Last"});
    int indexSignalCount = 0;
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.selectCell(0, 1);
    view.currentIndexChanged.connect([&](const snf::ModelIndex&) { ++indexSignalCount; });
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    ASSERT_TRUE(model.removeColumns(1, 1));

    EXPECT_FALSE(view.currentIndex().isValid());
    EXPECT_TRUE(view.selectedIndexes().empty());
    EXPECT_EQ(model.data(0, 1), "Lovelace");
    EXPECT_EQ(indexSignalCount, 1);
    EXPECT_EQ(selectionSignalCount, 1);
}

TEST_F(TableViewFixture, columnRemovalShiftsSelectionAfterRemovedRange)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Augusta", "Lovelace"}}, {"First", "Middle", "Last"});

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.selectCell(0, 2);

    ASSERT_TRUE(model.removeColumns(0, 1));

    EXPECT_EQ(view.currentIndex(), model.index(0, 1));
    EXPECT_EQ(view.selectedIndexes(), std::vector<snf::ModelIndex>({model.index(0, 1)}));
    EXPECT_EQ(model.data(0, 1), "Lovelace");
}

TEST_F(TableViewFixture, displayOptionsRoundTrip)
{
    snf::widgets::TableView view;

    view.setShowHorizontalHeader(false);
    view.setShowGrid(false);
    view.setStretchLastColumn(true);
    view.setRowSelectionEnabled(false);
    view.setSelectionMode(snf::widgets::TableSelectionMode::Multiple);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Columns);

    EXPECT_FALSE(view.showHorizontalHeader());
    EXPECT_FALSE(view.showGrid());
    EXPECT_TRUE(view.stretchLastColumn());
    EXPECT_FALSE(view.rowSelectionEnabled());
    EXPECT_EQ(view.selectionMode(), snf::widgets::TableSelectionMode::Multiple);
    EXPECT_EQ(view.selectionBehavior(), snf::widgets::TableSelectionBehavior::Columns);
}

TEST_F(TableViewFixture, sizeHintReflectsRowsAndColumns)
{
    snf::widgets::test::ImGuiInteractionHarness harness;
    snf::widgets::TableView view;
    TestTableModel model(
        {{"Ada", "Lovelace", "Analytical Engine"}, {"Grace", "Hopper", "Compiler"}},
        {"First", "Last", "Notes"});

    view.setModel(&model);

    harness.beginFrame();
    const snf::widgets::Size hint = view.sizeHint();
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    harness.endFrame();

    EXPECT_GE(hint.width, 360.0f);
    EXPECT_GT(hint.height, 40.0f);
}

TEST_F(TableViewFixture, multipleRowSelectionExpandsToCellIndexes)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setSelectionMode(snf::widgets::TableSelectionMode::Multiple);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Rows);
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    view.selectRow(0);
    view.selectRow(1);

    const std::vector<int> rows = view.selectedRows();
    const std::vector<snf::ModelIndex> indexes = view.selectedIndexes();

    EXPECT_EQ(rows, std::vector<int>({0, 1}));
    ASSERT_EQ(indexes.size(), 4u);
    EXPECT_EQ(indexes[0], model.index(0, 0));
    EXPECT_EQ(indexes[1], model.index(0, 1));
    EXPECT_EQ(indexes[2], model.index(1, 0));
    EXPECT_EQ(indexes[3], model.index(1, 1));
    EXPECT_EQ(selectionSignalCount, 2);
}

TEST_F(TableViewFixture, columnSelectionExpandsToCellIndexes)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});

    view.setModel(&model);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Columns);
    view.selectColumn(1);

    EXPECT_EQ(view.selectedColumns(), std::vector<int>({1}));
    ASSERT_EQ(view.selectedIndexes().size(), 2u);
    EXPECT_EQ(view.selectedIndexes()[0], model.index(0, 1));
    EXPECT_EQ(view.selectedIndexes()[1], model.index(1, 1));
}

TEST_F(TableViewFixture, selectionModeNoneKeepsCurrentButDoesNotSelect)
{
    snf::widgets::TableView view;
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });
    view.setSelectionMode(snf::widgets::TableSelectionMode::None);
    view.selectCell(0, 1);

    EXPECT_EQ(view.currentIndex(), model.index(0, 1));
    EXPECT_TRUE(view.selectedIndexes().empty());
    EXPECT_EQ(selectionSignalCount, 0);
}

TEST_F(TableViewFixture, mouseClickSelectsCellAndEmitsModelIndex)
{
    snf::widgets::test::ImGuiInteractionHarness harness;
    snf::widgets::test::TestWidget<snf::widgets::TableView> view;
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});
    int clickedCount = 0;
    snf::ModelIndex clickedIndex;

    view.setModel(&model);
    view.setShowHorizontalHeader(false);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.indexClicked.connect([&](const snf::ModelIndex& index) {
        ++clickedCount;
        clickedIndex = index;
    });

    renderWarmup(harness, view);
    clickTableAt(harness, view, tableCellCenter(0, 1));

    EXPECT_EQ(clickedCount, 1);
    EXPECT_EQ(clickedIndex, model.index(0, 1));
    EXPECT_EQ(view.currentIndex(), model.index(0, 1));
    EXPECT_EQ(view.selectedIndexes(), std::vector<snf::ModelIndex>({model.index(0, 1)}));
}

TEST_F(TableViewFixture, ctrlClickTogglesMultipleCellSelection)
{
    snf::widgets::test::ImGuiInteractionHarness harness;
    snf::widgets::test::TestWidget<snf::widgets::TableView> view;
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});
    int selectionSignalCount = 0;

    view.setModel(&model);
    view.setShowHorizontalHeader(false);
    view.setSelectionBehavior(snf::widgets::TableSelectionBehavior::Cells);
    view.setSelectionMode(snf::widgets::TableSelectionMode::Multiple);
    view.selectionChanged.connect([&]() { ++selectionSignalCount; });

    renderWarmup(harness, view);
    clickTableAt(harness, view, tableCellCenter(0, 1));
    clickTableAt(harness, view, tableCellCenter(1, 1), true);

    EXPECT_EQ(view.selectedIndexes(), std::vector<snf::ModelIndex>({model.index(0, 1), model.index(1, 1)}));

    clickTableAt(harness, view, tableCellCenter(1, 1), true);

    EXPECT_EQ(view.selectedIndexes(), std::vector<snf::ModelIndex>({model.index(0, 1)}));
    EXPECT_EQ(selectionSignalCount, 3);
}
