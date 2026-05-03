#pragma once

/**
 * @file SqlDatabase.h
 * @brief Abstract SQL database connection interface.
 * @ingroup SNFDatabase
 */

#include "SNFCore/Node.h"

#include <memory>
#include <string>

namespace snf {

class SqlQuery;
class SqlQueryDriver;

/**
 * @class SqlDatabase
 * @ingroup SNFDatabase
 * @brief Base interface for SQL database connections.
 */
class SqlDatabase : public Node
{
public:
    enum class DatabaseError
    {
        NoError,
        OpenFailed,
        QueryFailed,
        InvalidOperation
    };

    explicit SqlDatabase(Node* parent = nullptr);
    ~SqlDatabase() override;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const noexcept = 0;

    bool execute(const std::string& sql);

    DatabaseError lastError() const noexcept;
    const std::string& errorString() const noexcept;

protected:
    void update() override;

    virtual std::unique_ptr<SqlQueryDriver> createQueryDriver() = 0;

    void clearError() const;
    void setError(DatabaseError error, std::string errorString) const;

private:
    friend class SqlQuery;

    mutable DatabaseError m_lastError = DatabaseError::NoError;
    mutable std::string m_errorString;
};

}  // namespace snf
