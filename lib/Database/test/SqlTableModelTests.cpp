#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/TempFile.h"
#include "SNFCore/Variant.h"
#include "SNFDatabase/SqliteDatabase.h"
#include "SNFDatabase/SqlTableModel.h"

#include <cstdint>

using namespace snf;

namespace {

class SqlTableModelTest : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }

    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
};

/// Helper that opens a temporary SQLite database and seeds a simple table.
class TemporaryUsersDatabase
{
public:
    bool open()
    {
        if (! file.open()) {
            return false;
        }
        file.close();
        database.setDatabaseName(file.filePath());
        if (! database.open()) {
            return false;
        }
        if (! database.execute(
                "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)"))
        {
            return false;
        }
        if (! database.execute("INSERT INTO users (name, age) VALUES ('Alice', 30)")) {
            return false;
        }
        if (! database.execute("INSERT INTO users (name, age) VALUES ('Bob', 25)")) {
            return false;
        }
        if (! database.execute("INSERT INTO users (name, age) VALUES ('Carol', 40)")) {
            return false;
        }
        return true;
    }

    TempFile file;
    SqliteDatabase database;
};

}  // namespace

// ---------------------------------------------------------------------------
// select()
// ---------------------------------------------------------------------------

TEST_F(SqlTableModelTest, SelectLoadsRowsAndColumns)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_EQ(model.rowCount(), 3);
    EXPECT_EQ(model.columnCount(), 3);
}

TEST_F(SqlTableModelTest, SelectPopulatesHeaderNames)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_EQ(model.headerData(0), "id");
    EXPECT_EQ(model.headerData(1), "name");
    EXPECT_EQ(model.headerData(2), "age");
}

TEST_F(SqlTableModelTest, SelectPopulatesRowData)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_EQ(model.data(0, 1), "Alice");
    EXPECT_EQ(model.data(1, 1), "Bob");
    EXPECT_EQ(model.data(2, 1), "Carol");
}

TEST_F(SqlTableModelTest, SelectEmitsModelReset)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");

    int resetCount = 0;
    model.modelReset.connect([&]() { ++resetCount; });

    ASSERT_TRUE(model.select()) << model.errorString();
    EXPECT_EQ(resetCount, 1);
}

TEST_F(SqlTableModelTest, SelectWithNoTableNameReturnsError)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);

    EXPECT_FALSE(model.select());
    EXPECT_EQ(model.lastError(), SqlDatabase::DatabaseError::InvalidOperation);
    EXPECT_FALSE(model.errorString().empty());
}

TEST_F(SqlTableModelTest, SelectWithInvalidTableReturnsError)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("nonexistent_table");

    EXPECT_FALSE(model.select());
    EXPECT_NE(model.lastError(), SqlDatabase::DatabaseError::NoError);
    EXPECT_FALSE(model.errorString().empty());
}

TEST_F(SqlTableModelTest, SelectClearsPreviousRowsOnReload)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();
    ASSERT_EQ(model.rowCount(), 3);

    ASSERT_TRUE(fixture.database.execute("DELETE FROM users WHERE name='Bob'"));

    ASSERT_TRUE(model.select()) << model.errorString();
    EXPECT_EQ(model.rowCount(), 2);
}

// ---------------------------------------------------------------------------
// Bounds & read-only data access
// ---------------------------------------------------------------------------

TEST_F(SqlTableModelTest, DataReturnsEmptyStringForOutOfBoundsCell)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_EQ(model.data(-1, 0), "");
    EXPECT_EQ(model.data(0, -1), "");
    EXPECT_EQ(model.data(999, 0), "");
    EXPECT_EQ(model.data(0, 999), "");
}

TEST_F(SqlTableModelTest, HeaderDataReturnsEmptyStringForOutOfBounds)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_EQ(model.headerData(-1), "");
    EXPECT_EQ(model.headerData(999), "");
}

TEST_F(SqlTableModelTest, AllCellsAreEditable)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    for (int r = 0; r < model.rowCount(); ++r) {
        for (int c = 0; c < model.columnCount(); ++c) {
            EXPECT_TRUE(model.isEditable(r, c)) << "row=" << r << " col=" << c;
        }
    }
}

TEST_F(SqlTableModelTest, OutOfBoundsCellIsNotEditable)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_FALSE(model.isEditable(-1, 0));
    EXPECT_FALSE(model.isEditable(0, -1));
    EXPECT_FALSE(model.isEditable(999, 0));
}

// ---------------------------------------------------------------------------
// setData() — buffering / pending edits
// ---------------------------------------------------------------------------

TEST_F(SqlTableModelTest, SetDataBuffersPendingEdit)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_TRUE(model.setData(0, 1, "Alicia"));
    EXPECT_EQ(model.data(0, 1), "Alicia");
    EXPECT_TRUE(model.isDirty());
}

TEST_F(SqlTableModelTest, SetDataEmitsDataChanged)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    int emittedRow = -1;
    int emittedCol = -1;
    model.dataChanged.connect([&](int r, int c) {
        emittedRow = r;
        emittedCol = c;
    });

    model.setData(1, 2, "99");

    EXPECT_EQ(emittedRow, 1);
    EXPECT_EQ(emittedCol, 2);
}

TEST_F(SqlTableModelTest, SetDataDoesNotEmitWhenValueUnchanged)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    int changeCount = 0;
    model.dataChanged.connect([&](int, int) { ++changeCount; });

    const std::string currentValue = model.data(0, 1);
    model.setData(0, 1, currentValue);

    EXPECT_EQ(changeCount, 0);
    EXPECT_FALSE(model.isDirty());
}

TEST_F(SqlTableModelTest, SetDataReturnsFalseForOutOfBoundsCell)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_FALSE(model.setData(-1, 0, "x"));
    EXPECT_FALSE(model.setData(0, 999, "x"));
    EXPECT_FALSE(model.isDirty());
}

TEST_F(SqlTableModelTest, ModelIsNotDirtyAfterSelect)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_FALSE(model.isDirty());
}

// ---------------------------------------------------------------------------
// revert()
// ---------------------------------------------------------------------------

TEST_F(SqlTableModelTest, RevertDiscardsAllPendingEdits)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(0, 1, "Modified");
    model.setData(1, 2, "99");
    ASSERT_TRUE(model.isDirty());

    model.revert();

    EXPECT_FALSE(model.isDirty());
    EXPECT_EQ(model.data(0, 1), "Alice");
    EXPECT_EQ(model.data(1, 2), "25");
}

TEST_F(SqlTableModelTest, RevertEmitsDataChangedForEachRevertedCell)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(0, 1, "X");
    model.setData(2, 2, "99");

    int changeCount = 0;
    model.dataChanged.connect([&](int, int) { ++changeCount; });

    model.revert();

    EXPECT_EQ(changeCount, 2);
}

TEST_F(SqlTableModelTest, RevertOnCleanModelDoesNothing)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    int changeCount = 0;
    model.dataChanged.connect([&](int, int) { ++changeCount; });

    model.revert();

    EXPECT_EQ(changeCount, 0);
    EXPECT_FALSE(model.isDirty());
}

// ---------------------------------------------------------------------------
// submit()
// ---------------------------------------------------------------------------

TEST_F(SqlTableModelTest, SubmitWritesEditsToDatabase)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(0, 1, "Alicia");
    ASSERT_TRUE(model.submit()) << model.errorString();

    // Reload from the database to confirm the change persisted.
    ASSERT_TRUE(model.select()) << model.errorString();
    EXPECT_EQ(model.data(0, 1), "Alicia");
}

TEST_F(SqlTableModelTest, SubmitClearsDirtyFlag)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(1, 2, "99");
    ASSERT_TRUE(model.submit()) << model.errorString();

    EXPECT_FALSE(model.isDirty());
}

TEST_F(SqlTableModelTest, SubmitOnCleanModelReturnsTrue)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_TRUE(model.submit());
    EXPECT_EQ(model.lastError(), SqlDatabase::DatabaseError::NoError);
}

TEST_F(SqlTableModelTest, SubmitPersistsMultipleRowsAndColumns)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(0, 1, "AliceNew");
    model.setData(0, 2, "31");
    model.setData(2, 1, "Carolyn");

    ASSERT_TRUE(model.submit()) << model.errorString();
    ASSERT_TRUE(model.select()) << model.errorString();

    EXPECT_EQ(model.data(0, 1), "AliceNew");
    EXPECT_EQ(model.data(0, 2), "31");
    EXPECT_EQ(model.data(2, 1), "Carolyn");
}

TEST_F(SqlTableModelTest, SubmitDefaultsToFirstColumnAsPrimaryKey)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    // No explicit setPrimaryKeyColumn — should use column 0 (id).
    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(0, 1, "DefaultPK");
    ASSERT_TRUE(model.submit()) << model.errorString();

    ASSERT_TRUE(model.select()) << model.errorString();
    EXPECT_EQ(model.data(0, 1), "DefaultPK");
}

TEST_F(SqlTableModelTest, SubmitWithNoTableNameReturnsError)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    // Force a dirty state without select() by using a seeded model.
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();
    model.setData(0, 1, "X");

    // Now clear the table name to trigger the error path.
    model.setTable("");

    EXPECT_FALSE(model.submit());
    EXPECT_EQ(model.lastError(), SqlDatabase::DatabaseError::InvalidOperation);
}

TEST_F(SqlTableModelTest, SubmitWithInvalidPrimaryKeyColumnReturnsError)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("nonexistent_pk");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(0, 1, "X");

    EXPECT_FALSE(model.submit());
    EXPECT_EQ(model.lastError(), SqlDatabase::DatabaseError::InvalidOperation);
    EXPECT_FALSE(model.errorString().empty());
}

TEST_F(SqlTableModelTest, SubmitValueWithSingleQuoteIsEscapedCorrectly)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(0, 1, "O'Brien");
    ASSERT_TRUE(model.submit()) << model.errorString();

    ASSERT_TRUE(model.select()) << model.errorString();
    EXPECT_EQ(model.data(0, 1), "O'Brien");
}

// ---------------------------------------------------------------------------
// select() after submit() reloads from DB
// ---------------------------------------------------------------------------

TEST_F(SqlTableModelTest, SelectAfterSubmitReflectsPersistedChanges)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    model.setData(1, 1, "Bobby");
    ASSERT_TRUE(model.submit()) << model.errorString();

    // Simulate a fresh model reloading from the same DB.
    SqlTableModel model2(fixture.database);
    model2.setTable("users");
    ASSERT_TRUE(model2.select()) << model2.errorString();

    EXPECT_EQ(model2.data(1, 1), "Bobby");
}

// ---------------------------------------------------------------------------
// Variant typed API
// ---------------------------------------------------------------------------

TEST_F(SqlTableModelTest, TypedDataReturnsInt64ForIntegerColumn)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    // Column 0 is INTEGER PRIMARY KEY (id), column 2 is INTEGER (age).
    const snf::Variant ageVal = model.data(model.index(0, 2), ModelDataRole::Display);
    ASSERT_TRUE(ageVal.holds<std::int64_t>());
    EXPECT_EQ(ageVal.toInt64(), 30);
}

TEST_F(SqlTableModelTest, TypedDataReturnsStringForTextColumn)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    const snf::Variant nameVal = model.data(model.index(1, 1), ModelDataRole::Display);
    ASSERT_TRUE(nameVal.holds<std::string>());
    EXPECT_EQ(nameVal.toString(), "Bob");
}

TEST_F(SqlTableModelTest, TypedDataReturnsDoubleForRealColumn)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    // Create a table with a REAL column.
    ASSERT_TRUE(fixture.database.execute(
        "CREATE TABLE measurements (id INTEGER PRIMARY KEY, value REAL)"));
    ASSERT_TRUE(fixture.database.execute(
        "INSERT INTO measurements (value) VALUES (3.14)"));

    SqlTableModel model(fixture.database);
    model.setTable("measurements");
    ASSERT_TRUE(model.select()) << model.errorString();

    const snf::Variant val = model.data(model.index(0, 1), ModelDataRole::Display);
    ASSERT_TRUE(val.holds<double>());
    EXPECT_DOUBLE_EQ(val.toDouble(), 3.14);
}

TEST_F(SqlTableModelTest, TypedDataReturnsMonostateForNullCell)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    ASSERT_TRUE(fixture.database.execute(
        "CREATE TABLE nullable (id INTEGER PRIMARY KEY, val TEXT)"));
    ASSERT_TRUE(fixture.database.execute(
        "INSERT INTO nullable (val) VALUES (NULL)"));

    SqlTableModel model(fixture.database);
    model.setTable("nullable");
    ASSERT_TRUE(model.select()) << model.errorString();

    const snf::Variant val = model.data(model.index(0, 1), ModelDataRole::Display);
    EXPECT_TRUE(val.isNull());
}

TEST_F(SqlTableModelTest, TypedDataReturnsMonostateForInvalidIndex)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    // Invalid ModelIndex (default-constructed).
    const snf::Variant val = model.data(ModelIndex{}, ModelDataRole::Display);
    EXPECT_TRUE(val.isNull());
}

TEST_F(SqlTableModelTest, SetDataViaModelIndexBuffersTypedValue)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    const ModelIndex idx = model.index(0, 2);
    ASSERT_TRUE(model.setData(idx, snf::Variant(std::int64_t{99}), ModelDataRole::Edit));

    const snf::Variant val = model.data(idx, ModelDataRole::Edit);
    ASSERT_TRUE(val.holds<std::int64_t>());
    EXPECT_EQ(val.toInt64(), 99);
    EXPECT_TRUE(model.isDirty());
}

TEST_F(SqlTableModelTest, SetDataViaModelIndexDoesNotEmitWhenValueUnchanged)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    const ModelIndex idx = model.index(0, 2);
    const snf::Variant current = model.data(idx, ModelDataRole::Edit);

    int changeCount = 0;
    model.dataChanged.connect([&](int, int) { ++changeCount; });

    model.setData(idx, current, ModelDataRole::Edit);

    EXPECT_EQ(changeCount, 0);
    EXPECT_FALSE(model.isDirty());
}

TEST_F(SqlTableModelTest, SetDataDecorationRoleReturnsFalse)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    ASSERT_TRUE(model.select()) << model.errorString();

    const ModelIndex idx = model.index(0, 1);
    EXPECT_FALSE(model.setData(idx, snf::Variant(std::string{"X"}), ModelDataRole::Decoration));
    EXPECT_FALSE(model.isDirty());
}

TEST_F(SqlTableModelTest, SubmitPersistsInt64Edit)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    SqlTableModel model(fixture.database);
    model.setTable("users");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    const ModelIndex idx = model.index(0, 2);
    ASSERT_TRUE(model.setData(idx, snf::Variant(std::int64_t{55}), ModelDataRole::Edit));
    ASSERT_TRUE(model.submit()) << model.errorString();

    ASSERT_TRUE(model.select()) << model.errorString();
    const snf::Variant val = model.data(model.index(0, 2), ModelDataRole::Display);
    ASSERT_TRUE(val.holds<std::int64_t>());
    EXPECT_EQ(val.toInt64(), 55);
}

TEST_F(SqlTableModelTest, SubmitPersistsDoubleEdit)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    ASSERT_TRUE(fixture.database.execute(
        "CREATE TABLE scores (id INTEGER PRIMARY KEY, score REAL)"));
    ASSERT_TRUE(fixture.database.execute("INSERT INTO scores (score) VALUES (1.5)"));

    SqlTableModel model(fixture.database);
    model.setTable("scores");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    const ModelIndex idx = model.index(0, 1);
    ASSERT_TRUE(model.setData(idx, snf::Variant(9.81), ModelDataRole::Edit));
    ASSERT_TRUE(model.submit()) << model.errorString();

    ASSERT_TRUE(model.select()) << model.errorString();
    const snf::Variant val = model.data(model.index(0, 1), ModelDataRole::Display);
    ASSERT_TRUE(val.holds<double>());
    EXPECT_DOUBLE_EQ(val.toDouble(), 9.81);
}

TEST_F(SqlTableModelTest, SubmitPersistsNullEdit)
{
    TemporaryUsersDatabase fixture;
    ASSERT_TRUE(fixture.open());

    ASSERT_TRUE(fixture.database.execute(
        "CREATE TABLE nullable (id INTEGER PRIMARY KEY, val TEXT)"));
    ASSERT_TRUE(fixture.database.execute(
        "INSERT INTO nullable (val) VALUES ('something')"));

    SqlTableModel model(fixture.database);
    model.setTable("nullable");
    model.setPrimaryKeyColumn("id");
    ASSERT_TRUE(model.select()) << model.errorString();

    const ModelIndex idx = model.index(0, 1);
    ASSERT_TRUE(model.setData(idx, snf::Variant{}, ModelDataRole::Edit));
    ASSERT_TRUE(model.submit()) << model.errorString();

    ASSERT_TRUE(model.select()) << model.errorString();
    const snf::Variant val = model.data(model.index(0, 1), ModelDataRole::Display);
    EXPECT_TRUE(val.isNull());
}

