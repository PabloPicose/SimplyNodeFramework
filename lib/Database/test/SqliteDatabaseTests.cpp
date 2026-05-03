#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/TempFile.h"
#include "SNFDatabase/SqlQuery.h"
#include "SNFDatabase/SqliteDatabase.h"

#include <string>

using namespace snf;

namespace {

class SqliteDatabaseTest : public ::testing::Test
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

class TemporarySqliteDatabase
{
public:
    bool open()
    {
        if (! file.open()) {
            return false;
        }

        file.close();
        database.setDatabaseName(file.filePath());
        return database.open();
    }

    TempFile file;
    SqliteDatabase database;
};

}  // namespace

TEST_F(SqliteDatabaseTest, OpenCreatesConnectionToTemporaryDatabase)
{
    TemporarySqliteDatabase fixture;

    ASSERT_TRUE(fixture.open()) << fixture.database.errorString();

    EXPECT_TRUE(fixture.database.isOpen());
    EXPECT_FALSE(fixture.database.databaseName().empty());
}

TEST_F(SqliteDatabaseTest, ExecuteCreatesTableAndInsertsRows)
{
    TemporarySqliteDatabase fixture;
    ASSERT_TRUE(fixture.open()) << fixture.database.errorString();

    ASSERT_TRUE(fixture.database.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY, text TEXT)"))
        << fixture.database.errorString();
    ASSERT_TRUE(fixture.database.execute("INSERT INTO notes (text) VALUES ('first')"))
        << fixture.database.errorString();

    SqlQuery query(fixture.database);
    ASSERT_TRUE(query.exec("SELECT id, text FROM notes")) << query.errorString();
    ASSERT_TRUE(query.next()) << query.errorString();

    EXPECT_EQ(query.value(0), "1");
    EXPECT_EQ(query.value(1), "first");
    EXPECT_FALSE(query.next());
}

TEST_F(SqliteDatabaseTest, QueryIteratesResultRowsAndReportsColumns)
{
    TemporarySqliteDatabase fixture;
    ASSERT_TRUE(fixture.open()) << fixture.database.errorString();
    ASSERT_TRUE(fixture.database.execute("CREATE TABLE items (name TEXT, quantity INTEGER)"))
        << fixture.database.errorString();
    ASSERT_TRUE(fixture.database.execute("INSERT INTO items (name, quantity) VALUES ('apples', 4)"))
        << fixture.database.errorString();
    ASSERT_TRUE(fixture.database.execute("INSERT INTO items (name, quantity) VALUES ('oranges', 7)"))
        << fixture.database.errorString();

    SqlQuery query(fixture.database);
    ASSERT_TRUE(query.exec("SELECT name, quantity FROM items ORDER BY quantity")) << query.errorString();

    EXPECT_EQ(query.columnCount(), 2);
    EXPECT_EQ(query.columnName(0), "name");
    EXPECT_EQ(query.columnName(1), "quantity");

    ASSERT_TRUE(query.next()) << query.errorString();
    EXPECT_EQ(query.value(0), "apples");
    EXPECT_EQ(query.value(1), "4");
    EXPECT_FALSE(query.isNull(0));

    ASSERT_TRUE(query.next()) << query.errorString();
    EXPECT_EQ(query.value(0), "oranges");
    EXPECT_EQ(query.value(1), "7");

    EXPECT_FALSE(query.next());
}

TEST_F(SqliteDatabaseTest, QueryReportsNullValues)
{
    TemporarySqliteDatabase fixture;
    ASSERT_TRUE(fixture.open()) << fixture.database.errorString();
    ASSERT_TRUE(fixture.database.execute("CREATE TABLE values_table (value TEXT)")) << fixture.database.errorString();
    ASSERT_TRUE(fixture.database.execute("INSERT INTO values_table (value) VALUES (NULL)"))
        << fixture.database.errorString();

    SqlQuery query(fixture.database);
    ASSERT_TRUE(query.exec("SELECT value FROM values_table")) << query.errorString();
    ASSERT_TRUE(query.next()) << query.errorString();

    EXPECT_TRUE(query.isNull(0));
    EXPECT_EQ(query.value(0), "");
}

TEST_F(SqliteDatabaseTest, InvalidSqlReportsQueryError)
{
    TemporarySqliteDatabase fixture;
    ASSERT_TRUE(fixture.open()) << fixture.database.errorString();

    SqlQuery query(fixture.database);

    EXPECT_FALSE(query.exec("SELCT broken"));
    EXPECT_EQ(query.lastError(), SqlDatabase::DatabaseError::QueryFailed);
    EXPECT_FALSE(query.errorString().empty());
    EXPECT_EQ(fixture.database.lastError(), SqlDatabase::DatabaseError::QueryFailed);
}

TEST_F(SqliteDatabaseTest, ClosedDatabaseRejectsQuery)
{
    TemporarySqliteDatabase fixture;
    ASSERT_TRUE(fixture.open()) << fixture.database.errorString();
    fixture.database.close();

    SqlQuery query(fixture.database);

    EXPECT_FALSE(query.exec("CREATE TABLE rejected (id INTEGER)"));
    EXPECT_EQ(query.lastError(), SqlDatabase::DatabaseError::InvalidOperation);
    EXPECT_FALSE(query.errorString().empty());
}
