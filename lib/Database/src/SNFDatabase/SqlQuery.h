#pragma once

/**
 * @file SqlQuery.h
 * @brief SQL query facade for SNF database connections.
 * @ingroup SNFDatabase
 */

#include "SNFDatabase/SqlDatabase.h"

#include <SNFCore/Variant.h>

#include <memory>
#include <string>

namespace snf {

class SqlQueryDriver
{
public:
    virtual ~SqlQueryDriver();

    virtual bool exec(const std::string& sql) = 0;
    virtual bool next() = 0;
    virtual void finish() = 0;

    virtual int columnCount() const = 0;
    virtual std::string columnName(int column) const = 0;
    virtual std::string value(int column) const = 0;
    virtual Variant typedValue(int column) const = 0;
    virtual bool isNull(int column) const = 0;

    virtual SqlDatabase::DatabaseError lastError() const noexcept = 0;
    virtual const std::string& errorString() const noexcept = 0;
};

/**
 * @class SqlQuery
 * @ingroup SNFDatabase
 * @brief Executes SQL statements and iterates over result rows.
 */
class SqlQuery
{
public:
    explicit SqlQuery(SqlDatabase& database);
    ~SqlQuery();

    SqlQuery(const SqlQuery&) = delete;
    SqlQuery& operator=(const SqlQuery&) = delete;

    SqlQuery(SqlQuery&&) noexcept;
    SqlQuery& operator=(SqlQuery&&) noexcept;

    bool exec(const std::string& sql);
    bool next();
    void finish();

    int columnCount() const;
    std::string columnName(int column) const;
    std::string value(int column) const;
    Variant typedValue(int column) const;
    bool isNull(int column) const;

    SqlDatabase::DatabaseError lastError() const noexcept;
    const std::string& errorString() const noexcept;

private:
    void clearError();
    void setError(SqlDatabase::DatabaseError error, std::string errorString);

private:
    SqlDatabase* m_database = nullptr;
    std::unique_ptr<SqlQueryDriver> m_driver;
    SqlDatabase::DatabaseError m_lastError = SqlDatabase::DatabaseError::NoError;
    std::string m_errorString;
};

}  // namespace snf
