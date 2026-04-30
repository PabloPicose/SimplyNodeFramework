#include <gtest/gtest.h>

#include "SNFWidgets/DataWidgetMapper.h"
#include "SNFWidgets/LineEdit.h"

#include <SNFCore/Application.h>

#include <string>
#include <vector>

namespace {

class EditableTableModel : public snf::AbstractTableModel
{
public:
    EditableTableModel(std::vector<std::vector<std::string>> rows, std::vector<std::string> headers)
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
        if (section < 0 || section >= columnCount()) {
            return {};
        }
        return m_headers[static_cast<std::size_t>(section)];
    }

    bool isEditable(int row, int column) const override
    {
        return isValidCell(row, column);
    }

    bool setData(int row, int column, const std::string& value) override
    {
        if (! isValidCell(row, column)) {
            return false;
        }

        auto& cell = m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
        if (cell == value) {
            return false;
        }

        cell = value;
        notifyDataChanged(row, column);
        return true;
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
    std::vector<std::string> m_headers;
};

class DataWidgetMapperFixture : public ::testing::Test
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

TEST_F(DataWidgetMapperFixture, mapsCurrentRowIntoLineEdits)
{
    EditableTableModel model({{"Ada", "Lovelace", "Math"}, {"Grace", "Hopper", "COBOL"}},
                             {"First", "Last", "Field"});
    snf::widgets::LineEdit first;
    snf::widgets::LineEdit last;
    snf::widgets::LineEdit field;
    snf::widgets::DataWidgetMapper mapper;

    mapper.setModel(&model);
    mapper.addMapping(&first, 0);
    mapper.addMapping(&last, 1);
    mapper.addMapping(&field, 2);
    mapper.setCurrentIndex(model.index(1, 0));

    EXPECT_EQ(mapper.currentRow(), 1);
    EXPECT_EQ(first.text(), "Grace");
    EXPECT_EQ(last.text(), "Hopper");
    EXPECT_EQ(field.text(), "COBOL");
}

TEST_F(DataWidgetMapperFixture, invalidCurrentIndexClearsMappedWidgets)
{
    EditableTableModel model({{"Ada", "Lovelace", "Math"}}, {"First", "Last", "Field"});
    snf::widgets::LineEdit first;
    snf::widgets::LineEdit last;
    snf::widgets::DataWidgetMapper mapper;

    mapper.setModel(&model);
    mapper.addMapping(&first, 0);
    mapper.addMapping(&last, 1);
    mapper.setCurrentRow(0);
    mapper.setCurrentIndex(snf::ModelIndex());

    EXPECT_EQ(mapper.currentRow(), -1);
    EXPECT_EQ(first.text(), "");
    EXPECT_EQ(last.text(), "");
}

TEST_F(DataWidgetMapperFixture, submitCommitsChangedWidgets)
{
    EditableTableModel model({{"Ada", "Lovelace", "Math"}}, {"First", "Last", "Field"});
    snf::widgets::LineEdit first;
    snf::widgets::LineEdit last;
    snf::widgets::DataWidgetMapper mapper;
    int submittedCount = 0;

    mapper.setModel(&model);
    mapper.addMapping(&first, 0);
    mapper.addMapping(&last, 1);
    mapper.setCurrentRow(0);
    mapper.submitted.connect([&]() { ++submittedCount; });

    first.setText("Augusta Ada");
    last.setText("Byron");

    EXPECT_TRUE(mapper.submit());
    EXPECT_EQ(model.data(0, 0), "Augusta Ada");
    EXPECT_EQ(model.data(0, 1), "Byron");
    EXPECT_EQ(submittedCount, 1);
}

TEST_F(DataWidgetMapperFixture, revertRestoresModelValues)
{
    EditableTableModel model({{"Ada", "Lovelace", "Math"}}, {"First", "Last", "Field"});
    snf::widgets::LineEdit first;
    snf::widgets::DataWidgetMapper mapper;

    mapper.setModel(&model);
    mapper.addMapping(&first, 0);
    mapper.setCurrentRow(0);
    first.setText("Changed");

    mapper.revert();

    EXPECT_EQ(first.text(), "Ada");
    EXPECT_EQ(model.data(0, 0), "Ada");
}

TEST_F(DataWidgetMapperFixture, modelDataChangedRefreshesMappedColumn)
{
    EditableTableModel model({{"Ada", "Lovelace", "Math"}}, {"First", "Last", "Field"});
    snf::widgets::LineEdit first;
    snf::widgets::LineEdit last;
    snf::widgets::DataWidgetMapper mapper;

    mapper.setModel(&model);
    mapper.addMapping(&first, 0);
    mapper.addMapping(&last, 1);
    mapper.setCurrentRow(0);

    EXPECT_TRUE(model.setData(0, 1, "Byron"));

    EXPECT_EQ(first.text(), "Ada");
    EXPECT_EQ(last.text(), "Byron");
}

TEST_F(DataWidgetMapperFixture, rowInsertionAndRemovalTrackCurrentRow)
{
    EditableTableModel model({{"Ada"}, {"Grace"}, {"Linus"}}, {"Name"});
    snf::widgets::LineEdit name;
    snf::widgets::DataWidgetMapper mapper;
    int currentRowSignalCount = 0;

    mapper.setModel(&model);
    mapper.addMapping(&name, 0);
    mapper.setCurrentRow(2);
    mapper.currentRowChanged.connect([&](int) { ++currentRowSignalCount; });

    ASSERT_TRUE(model.insertRows(0, 1));
    EXPECT_EQ(mapper.currentRow(), 3);
    EXPECT_EQ(name.text(), "Linus");

    ASSERT_TRUE(model.removeRows(1, 1));
    EXPECT_EQ(mapper.currentRow(), 2);
    EXPECT_EQ(name.text(), "Linus");
    EXPECT_EQ(currentRowSignalCount, 2);
}

TEST_F(DataWidgetMapperFixture, removingCurrentRowClearsWidgets)
{
    EditableTableModel model({{"Ada"}, {"Grace"}}, {"Name"});
    snf::widgets::LineEdit name;
    snf::widgets::DataWidgetMapper mapper;

    mapper.setModel(&model);
    mapper.addMapping(&name, 0);
    mapper.setCurrentRow(1);

    ASSERT_TRUE(model.removeRows(1, 1));

    EXPECT_EQ(mapper.currentRow(), -1);
    EXPECT_EQ(name.text(), "");
}

TEST_F(DataWidgetMapperFixture, columnInsertionAndRemovalTrackMappings)
{
    EditableTableModel model({{"Ada", "Lovelace", "Math"}}, {"First", "Last", "Field"});
    snf::widgets::LineEdit last;
    snf::widgets::DataWidgetMapper mapper;

    mapper.setModel(&model);
    mapper.addMapping(&last, 1);
    mapper.setCurrentRow(0);

    ASSERT_TRUE(model.insertColumns(0, 1));
    EXPECT_EQ(mapper.mappedColumn(&last), 2);
    EXPECT_EQ(last.text(), "Lovelace");

    ASSERT_TRUE(model.removeColumns(1, 1));
    EXPECT_EQ(mapper.mappedColumn(&last), 1);
    EXPECT_EQ(last.text(), "Lovelace");
}

TEST_F(DataWidgetMapperFixture, removingMappedColumnClearsThatWidget)
{
    EditableTableModel model({{"Ada", "Lovelace", "Math"}}, {"First", "Last", "Field"});
    snf::widgets::LineEdit last;
    snf::widgets::DataWidgetMapper mapper;

    mapper.setModel(&model);
    mapper.addMapping(&last, 1);
    mapper.setCurrentRow(0);

    ASSERT_TRUE(model.removeColumns(1, 1));

    EXPECT_EQ(mapper.mappedColumn(&last), -1);
    EXPECT_EQ(last.text(), "");
}

TEST_F(DataWidgetMapperFixture, mappedWidgetsCanBeEnabledTogether)
{
    snf::widgets::LineEdit first;
    snf::widgets::LineEdit last;
    snf::widgets::DataWidgetMapper mapper;

    mapper.addMapping(&first, 0);
    mapper.addMapping(&last, 1);

    mapper.setMappedWidgetsEnabled(false);
    EXPECT_FALSE(first.isEnabled());
    EXPECT_FALSE(last.isEnabled());

    mapper.setMappedWidgetsEnabled(true);
    EXPECT_TRUE(first.isEnabled());
    EXPECT_TRUE(last.isEnabled());
}
