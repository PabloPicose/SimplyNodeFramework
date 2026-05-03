#include "SNFDatabase/SqliteDatabase.h"

#include "SNFDatabase/SqlQuery.h"

#include <sqlite3.h>

#include <utility>

namespace snf {

namespace {

bool validColumn(sqlite3_stmt* statement, int column)
{
    return statement != nullptr && column >= 0 && column < sqlite3_column_count(statement);
}

}  // namespace

class SqliteQueryDriver final : public SqlQueryDriver
{
public:
    explicit SqliteQueryDriver(SqliteDatabase& database)
        : m_database(database)
    {
    }

    ~SqliteQueryDriver() override { finish(); }

    bool exec(const std::string& sql) override
    {
        finish();
        clearError();

        sqlite3* database = m_database.handle();
        if (database == nullptr) {
            setError(SqlDatabase::DatabaseError::InvalidOperation, "SQLite database is not open");
            return false;
        }

        sqlite3_stmt* statement = nullptr;
        const int prepareResult = sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr);
        if (prepareResult != SQLITE_OK) {
            setError(SqlDatabase::DatabaseError::QueryFailed, sqlite3_errmsg(database));
            return false;
        }

        m_statement = statement;
        if (m_statement == nullptr) {
            return true;
        }

        if (sqlite3_column_count(m_statement) == 0) {
            int stepResult = SQLITE_OK;
            while ((stepResult = sqlite3_step(m_statement)) == SQLITE_ROW) {
            }

            if (stepResult != SQLITE_DONE) {
                setError(SqlDatabase::DatabaseError::QueryFailed, sqlite3_errmsg(database));
                finish();
                return false;
            }

            finish();
        }

        return true;
    }

    bool next() override
    {
        clearError();

        if (m_statement == nullptr) {
            return false;
        }

        const int stepResult = sqlite3_step(m_statement);
        if (stepResult == SQLITE_ROW) {
            return true;
        }

        if (stepResult == SQLITE_DONE) {
            finish();
            return false;
        }

        setError(SqlDatabase::DatabaseError::QueryFailed, sqlite3_errmsg(m_database.handle()));
        finish();
        return false;
    }

    void finish() override
    {
        if (m_statement != nullptr) {
            sqlite3_finalize(m_statement);
            m_statement = nullptr;
        }
    }

    int columnCount() const override
    {
        return m_statement ? sqlite3_column_count(m_statement) : 0;
    }

    std::string columnName(int column) const override
    {
        if (! validColumn(m_statement, column)) {
            return {};
        }

        const char* name = sqlite3_column_name(m_statement, column);
        return name != nullptr ? std::string(name) : std::string();
    }

    std::string value(int column) const override
    {
        if (! validColumn(m_statement, column) || isNull(column)) {
            return {};
        }

        const auto* text = sqlite3_column_text(m_statement, column);
        return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
    }

    Variant typedValue(int column) const override
    {
        if (! validColumn(m_statement, column)) {
            return {};
        }

        const int sqliteType = sqlite3_column_type(m_statement, column);
        switch (sqliteType) {
            case SQLITE_NULL:
                return {};
            case SQLITE_INTEGER:
                return static_cast<std::int64_t>(sqlite3_column_int64(m_statement, column));
            case SQLITE_FLOAT:
                return sqlite3_column_double(m_statement, column);
            case SQLITE_TEXT: {
                const auto* text = sqlite3_column_text(m_statement, column);
                return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
            }
            case SQLITE_BLOB:
            default: {
                // Represent blobs and unknown types as their text representation.
                const auto* text = sqlite3_column_text(m_statement, column);
                return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
            }
        }
    }

    bool isNull(int column) const override
    {
        if (! validColumn(m_statement, column)) {
            return true;
        }

        return sqlite3_column_type(m_statement, column) == SQLITE_NULL;
    }

    SqlDatabase::DatabaseError lastError() const noexcept override
    {
        return m_lastError;
    }

    const std::string& errorString() const noexcept override
    {
        return m_errorString;
    }

private:
    void clearError()
    {
        m_lastError = SqlDatabase::DatabaseError::NoError;
        m_errorString.clear();
    }

    void setError(SqlDatabase::DatabaseError error, std::string errorString)
    {
        m_lastError = error;
        m_errorString = std::move(errorString);
    }

private:
    SqliteDatabase& m_database;
    sqlite3_stmt* m_statement = nullptr;
    SqlDatabase::DatabaseError m_lastError = SqlDatabase::DatabaseError::NoError;
    std::string m_errorString;
};

SqliteDatabase::SqliteDatabase(Node* parent)
    : SqlDatabase(parent)
{
}

SqliteDatabase::SqliteDatabase(std::string databaseName, Node* parent)
    : SqlDatabase(parent)
    , m_databaseName(std::move(databaseName))
{
}

SqliteDatabase::~SqliteDatabase()
{
    close();
}

void SqliteDatabase::setDatabaseName(std::string databaseName)
{
    if (isOpen()) {
        close();
    }

    m_databaseName = std::move(databaseName);
    clearError();
}

const std::string& SqliteDatabase::databaseName() const noexcept
{
    return m_databaseName;
}

bool SqliteDatabase::open()
{
    if (isOpen()) {
        return true;
    }

    if (m_databaseName.empty()) {
        setError(DatabaseError::OpenFailed, "SQLite database name is empty");
        return false;
    }

    sqlite3* database = nullptr;
    const int openResult = sqlite3_open(m_databaseName.c_str(), &database);
    if (openResult != SQLITE_OK) {
        const std::string message =
            database != nullptr ? sqlite3_errmsg(database) : "Failed to open SQLite database";
        if (database != nullptr) {
            sqlite3_close(database);
        }
        setError(DatabaseError::OpenFailed, message);
        return false;
    }

    m_handle = database;
    clearError();
    return true;
}

void SqliteDatabase::close()
{
    if (m_handle != nullptr) {
        sqlite3_close_v2(m_handle);
        m_handle = nullptr;
    }
}

bool SqliteDatabase::isOpen() const noexcept
{
    return m_handle != nullptr;
}

std::unique_ptr<SqlQueryDriver> SqliteDatabase::createQueryDriver()
{
    return std::make_unique<SqliteQueryDriver>(*this);
}

sqlite3* SqliteDatabase::handle() const noexcept
{
    return m_handle;
}

}  // namespace snf
