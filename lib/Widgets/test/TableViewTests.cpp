#include <gtest/gtest.h>

#include "SNFWidgets/TableView.h"
#include <SNFCore/Application.h>
#include <SNFCore/AbstractTableModel.h>

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

}  // namespace

TEST_F(TableViewFixture, construction)
{
    snf::widgets::TableView view;

    EXPECT_EQ(view.model(), nullptr);
    EXPECT_TRUE(view.showHorizontalHeader());
    EXPECT_TRUE(view.showGrid());
    EXPECT_TRUE(view.rowSelectionEnabled());
    EXPECT_EQ(view.currentRow(), -1);
    EXPECT_EQ(view.currentColumn(), -1);
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
    int cellSignalCount = 0;

    view.setModel(&model);
    view.currentRowChanged.connect([&](int row) {
        ++rowSignalCount;
        EXPECT_EQ(row, 0);
    });
    view.currentCellChanged.connect([&](int row, int column) {
        ++cellSignalCount;
        EXPECT_EQ(row, 0);
        EXPECT_EQ(column, 1);
    });

    view.setCurrentCell(0, 1);

    EXPECT_EQ(view.currentRow(), 0);
    EXPECT_EQ(view.currentColumn(), 1);
    EXPECT_EQ(rowSignalCount, 1);
    EXPECT_EQ(cellSignalCount, 1);
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

TEST_F(TableViewFixture, displayOptionsRoundTrip)
{
    snf::widgets::TableView view;

    view.setShowHorizontalHeader(false);
    view.setShowGrid(false);
    view.setRowSelectionEnabled(false);

    EXPECT_FALSE(view.showHorizontalHeader());
    EXPECT_FALSE(view.showGrid());
    EXPECT_FALSE(view.rowSelectionEnabled());
}
