#pragma once

/**
 * @file SqliteDatabase.h
 * @brief SQLite implementation of the SQL database interface.
 * @ingroup SNFDatabase
 */

#include "SNFDatabase/SqlDatabase.h"

#include <string>

struct sqlite3;

namespace snf {

class SqliteQueryDriver;

/**
 * @class SqliteDatabase
 * @ingroup SNFDatabase
 * @brief SQL database connection backed by SQLite.
 */
class SqliteDatabase : public SqlDatabase
{
public:
    explicit SqliteDatabase(Node* parent = nullptr);
    explicit SqliteDatabase(std::string databaseName, Node* parent = nullptr);
    ~SqliteDatabase() override;

    void setDatabaseName(std::string databaseName);
    const std::string& databaseName() const noexcept;

    bool open() override;
    void close() override;
    bool isOpen() const noexcept override;

protected:
    std::unique_ptr<SqlQueryDriver> createQueryDriver() override;

private:
    sqlite3* handle() const noexcept;

private:
    friend class SqliteQueryDriver;

    std::string m_databaseName;
    sqlite3* m_handle = nullptr;
};

}  // namespace snf
