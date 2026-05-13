#pragma once

/**
 * @file SqlDatabaseProvider.h
 * @brief Interface for objects that expose a SQL database connection.
 * @ingroup SNFDatabase
 */

namespace snf {

class SqlDatabase;

/**
 * @class SqlDatabaseProvider
 * @ingroup SNFDatabase
 * @brief Abstract source of an active SQL database connection.
 */
class SqlDatabaseProvider
{
public:
    virtual ~SqlDatabaseProvider() = default;

    virtual SqlDatabase& database() = 0;
    virtual const SqlDatabase& database() const = 0;
};

}  // namespace snf