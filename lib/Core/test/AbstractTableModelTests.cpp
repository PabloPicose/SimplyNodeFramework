#include <gtest/gtest.h>

#include "SNFCore/AbstractTableModel.h"
#include "SNFCore/ModelIndex.h"

#include <cstdint>
#include <string>
#include <variant>
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

class ReadOnlyTableModel : public snf::AbstractTableModel
{
public:
    int rowCount() const override { return 0; }
    int columnCount() const override { return 0; }
    std::string data(int, int) const override { return {}; }
};

class VariantTableModel : public snf::AbstractTableModel
{
public:
    int rowCount() const override { return 1; }
    int columnCount() const override { return 4; }

    std::string data(int row, int column) const override
    {
        return snf::modelValueToString(data(index(row, column)));
    }

    snf::ModelValue data(const snf::ModelIndex& index, snf::ModelDataRole role = snf::ModelDataRole::Display) const override
    {
        if (! index.isValid() || index.model() != this || role != snf::ModelDataRole::Display) {
            return std::monostate{};
        }

        switch (index.column()) {
        case 0:
            return true;
        case 1:
            return 42;
        case 2:
            return 2.5;
        case 3:
            return std::string("text");
        default:
            return std::monostate{};
        }
    }
};

}  // namespace

TEST(AbstractTableModelTests, emptyModel)
{
    TestTableModel model;

    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(model.columnCount(), 0);
    EXPECT_EQ(model.data(0, 0), "");
    EXPECT_EQ(model.headerData(0), "");
}

TEST(AbstractTableModelTests, rowAndColumnCount)
{
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});

    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.columnCount(), 2);
}

TEST(AbstractTableModelTests, readsDataAndHeaders)
{
    TestTableModel model({{"Ada", "Lovelace"}, {"Grace", "Hopper"}}, {"First", "Last"});
    const snf::AbstractTableModel& base = model;

    EXPECT_EQ(model.data(0, 0), "Ada");
    EXPECT_EQ(model.data(1, 1), "Hopper");
    EXPECT_EQ(snf::modelValueToString(base.data(model.index(1, 1))), "Hopper");
    EXPECT_EQ(model.headerData(0), "First");
    EXPECT_EQ(model.headerData(1), "Last");
}

TEST(AbstractTableModelTests, modelIndexTracksModelRowAndColumn)
{
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});

    const snf::ModelIndex index = model.index(0, 1);

    EXPECT_TRUE(index.isValid());
    EXPECT_EQ(index.model(), &model);
    EXPECT_EQ(index.row(), 0);
    EXPECT_EQ(index.column(), 1);
    EXPECT_FALSE(model.index(-1, 0).isValid());
    EXPECT_FALSE(model.index(0, 8).isValid());
}

TEST(AbstractTableModelTests, invalidIndexesReturnEmptyStrings)
{
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    const snf::AbstractTableModel& base = model;

    EXPECT_EQ(model.data(-1, 0), "");
    EXPECT_EQ(model.data(0, -1), "");
    EXPECT_EQ(model.data(1, 0), "");
    EXPECT_EQ(model.data(0, 2), "");
    EXPECT_EQ(model.headerData(-1), "");
    EXPECT_EQ(model.headerData(2), "");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(base.data(snf::ModelIndex())));
}

TEST(AbstractTableModelTests, defaultOptionalApiIsReadOnly)
{
    ReadOnlyTableModel model;

    EXPECT_EQ(model.headerData(0), "");
    EXPECT_EQ(model.verticalHeaderData(0), "");
    EXPECT_FALSE(model.isEditable(0, 0));
    EXPECT_FALSE(model.setData(0, 0, "value"));
    EXPECT_FALSE(model.isEditable(snf::ModelIndex()));
    EXPECT_FALSE(model.setData(snf::ModelIndex(), snf::ModelValue(std::string("value"))));
}

TEST(AbstractTableModelTests, modelValueConvertsToDisplayString)
{
    EXPECT_EQ(snf::modelValueToString(std::monostate{}), "");
    EXPECT_EQ(snf::modelValueToString(true), "true");
    EXPECT_EQ(snf::modelValueToString(false), "false");
    EXPECT_EQ(snf::modelValueToString(42), "42");
    EXPECT_EQ(snf::modelValueToString(std::int64_t{9000000000LL}), "9000000000");
    EXPECT_EQ(snf::modelValueToString(2.5), "2.5");
    EXPECT_EQ(snf::modelValueToString(std::string("text")), "text");
}

TEST(AbstractTableModelTests, modelCanReturnTypedValues)
{
    VariantTableModel model;
    const snf::AbstractTableModel& base = model;

    EXPECT_EQ(std::get<bool>(base.data(model.index(0, 0))), true);
    EXPECT_EQ(std::get<int>(base.data(model.index(0, 1))), 42);
    EXPECT_EQ(std::get<double>(base.data(model.index(0, 2))), 2.5);
    EXPECT_EQ(std::get<std::string>(base.data(model.index(0, 3))), "text");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(base.data(model.index(0, 3), snf::ModelDataRole::Edit)));
}

TEST(AbstractTableModelTests, setDataEmitsDataChangedWhenValueChanges)
{
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    int count = 0;
    int changedRow = -1;
    int changedColumn = -1;

    model.dataChanged.connect([&](int row, int column) {
        ++count;
        changedRow = row;
        changedColumn = column;
    });

    EXPECT_TRUE(model.setData(0, 1, "Byron"));
    EXPECT_EQ(model.data(0, 1), "Byron");
    EXPECT_EQ(count, 1);
    EXPECT_EQ(changedRow, 0);
    EXPECT_EQ(changedColumn, 1);
}

TEST(AbstractTableModelTests, setDataAcceptsModelIndex)
{
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    snf::AbstractTableModel& base = model;
    const snf::ModelIndex index = model.index(0, 1);

    EXPECT_TRUE(base.isEditable(index));
    EXPECT_TRUE(base.setData(index, snf::ModelValue(std::string("Byron"))));
    EXPECT_EQ(snf::modelValueToString(base.data(index)), "Byron");
}

TEST(AbstractTableModelTests, setDataDoesNotEmitForSameOrInvalidValue)
{
    TestTableModel model({{"Ada", "Lovelace"}}, {"First", "Last"});
    int count = 0;
    model.dataChanged.connect([&](int, int) { ++count; });

    EXPECT_FALSE(model.setData(0, 0, "Ada"));
    EXPECT_FALSE(model.setData(5, 0, "Nope"));
    EXPECT_EQ(count, 0);
}

TEST(AbstractTableModelTests, resetEmitsModelReset)
{
    TestTableModel model({{"Ada"}}, {"Name"});
    int count = 0;
    model.modelReset.connect([&]() { ++count; });

    model.reset({{"Grace", "Hopper"}}, {"First", "Last"});

    EXPECT_EQ(count, 1);
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.columnCount(), 2);
    EXPECT_EQ(model.data(0, 1), "Hopper");
}
