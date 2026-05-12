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

    /**
     * @brief Opens the database connection.
     *
     * This method attempts to establish a connection to the database.
     *
     * @return True if the connection is successfully opened, false otherwise.
     */
    virtual bool open() = 0;

    /**
     * @brief Closes the database connection.
     *
     * This method closes the current database connection if it is open.
     */
    virtual void close() = 0;

    /**
     * @brief Checks if the database connection is open.
     *
     * @return True if the connection is open, false otherwise.
     */
    virtual bool isOpen() const noexcept = 0;

    /**
     * @brief Executes an SQL query.
     *
     * Executes the provided SQL query string on the database.
     *
     * @param sql The SQL query string to execute.
     * @return True if the query is successfully executed, false otherwise.
     */
    bool execute(const std::string& sql);

    /**
     * @brief Retrieves the last database error.
     *
     * @return The last error that occurred as a `DatabaseError` enum value.
     */
    DatabaseError lastError() const noexcept;

    /**
     * @brief Retrieves the last error message.
     *
     * @return A string describing the last error that occurred.
     */
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
