#pragma once

/**
 * @file MySqlDatabaseManager.h
 * @brief Thread-local manager for MySQL database connections.
 * @ingroup SNFDatabase
 */

#include "SNFCore/ThreadStorage.h"
#include "SNFDatabase/MySqlDatabase.h"
#include "SNFDatabase/SqlDatabaseProvider.h"

#include <memory>
#include <utility>

namespace snf {

/**
 * @class MySqlDatabaseManager
 * @ingroup SNFDatabase
 * @brief Provides one persistent MySQL connection per thread.
 *
 * The manager lazily creates a `MySqlDatabase` the first time a thread calls
 * `database()`. Each thread gets its own instance, and that instance remains
 * owned by the thread that created it.
 *
 * This class does not make the connection shareable across threads. It only
 * centralizes the creation and lookup of thread-owned connections.
 */
class MySqlDatabaseManager : public SqlDatabaseProvider
{
public:
    explicit MySqlDatabaseManager(MySqlDatabase::ConnectionOptions options);

    MySqlDatabaseManager(const MySqlDatabaseManager&) = delete;
    MySqlDatabaseManager& operator=(const MySqlDatabaseManager&) = delete;

    /**
     * @brief Returns the current thread's MySQL connection, creating it if needed.
     */
    MySqlDatabase& database() override;

    /**
     * @brief Returns the current thread's MySQL connection, creating it if needed.
     */
    const MySqlDatabase& database() const override;

    /** @brief Opens the current thread's connection. */
    bool open();

    /** @brief Closes the current thread's connection if it exists. */
    void close();

    /** @brief Returns whether the current thread's connection is open. */
    bool isOpen() const;

    /** @brief Returns the connection options used for future per-thread connections. */
    const MySqlDatabase::ConnectionOptions& connectionOptions() const noexcept;

private:
    MySqlDatabase& currentDatabase() const;

private:
    mutable ThreadStorage<std::unique_ptr<MySqlDatabase>> m_databases;
    MySqlDatabase::ConnectionOptions m_options;
};

inline MySqlDatabaseManager::MySqlDatabaseManager(MySqlDatabase::ConnectionOptions options)
    : m_options(std::move(options))
{
}

inline MySqlDatabase& MySqlDatabaseManager::currentDatabase() const
{
    auto& database = m_databases.localData();
    if (! database) {
        database = std::make_unique<MySqlDatabase>(m_options);
    }
    return *database;
}

inline MySqlDatabase& MySqlDatabaseManager::database()
{
    return currentDatabase();
}

inline const MySqlDatabase& MySqlDatabaseManager::database() const
{
    return currentDatabase();
}

inline bool MySqlDatabaseManager::open()
{
    return currentDatabase().open();
}

inline void MySqlDatabaseManager::close()
{
    currentDatabase().close();
}

inline bool MySqlDatabaseManager::isOpen() const
{
    return currentDatabase().isOpen();
}

inline const MySqlDatabase::ConnectionOptions& MySqlDatabaseManager::connectionOptions() const noexcept
{
    return m_options;
}

}  // namespace snf