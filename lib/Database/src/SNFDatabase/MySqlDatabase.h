#pragma once

/**
 * @file MySqlDatabase.h
 * @brief MySQL implementation of the SQL database interface.
 * @ingroup SNFDatabase
 */

#include <SNFCore/Connection.h>
#include <SNFCore/Timer.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "SNFDatabase/SqlDatabase.h"

namespace snf {

class MySqlQueryDriver;

/**
 * @class MySqlDatabase
 * @ingroup SNFDatabase
 * @brief SQL database connection backed by libmysqlclient.
 */
class MySqlDatabase : public SqlDatabase
{
public:
    /**
     * @enum FeatureFlag
     * @brief Flags to enable optional features for the database connection.
     *
     * This enumeration defines the optional features that can be enabled for the
     * `MySqlDatabase` class. These features control specific behaviors such as
     * connection monitoring and automatic reconnection.
     */
    enum FeatureFlag : std::uint32_t {
        NoFeatures = 0,          /**< No optional features are enabled. */
        PingTimeout = 1U << 0,   /**< Enables periodic ping checks to verify the connection status. */
        AutoReconnect = 1U << 1, /**< Enables automatic reconnection if the connection is lost. */
    };

    using FeatureFlags = std::uint32_t;

    struct ConnectionOptions {
        std::string hostName = "127.0.0.1";
        unsigned int port = 3306;
        std::string databaseName;
        std::string userName;
        std::string password;
    };

    explicit MySqlDatabase(Node* parent = nullptr);
    explicit MySqlDatabase(ConnectionOptions options, Node* parent = nullptr);
    ~MySqlDatabase() override;

    /**
     * @brief Sets the hostname for the database connection.
     *
     * If the database connection is open, it will be closed before setting the new hostname.
     *
     * @param hostName The hostname or IP address of the database server.
     */
    void setHostName(std::string hostName);

    /**
     * @brief Retrieves the hostname of the database connection.
     *
     * @return The hostname or IP address of the database server.
     */
    const std::string& hostName() const noexcept;

    /**
     * @brief Sets the port for the database connection.
     *
     * If the database connection is open, it will be closed before setting the new port.
     *
     * @param port The port number to connect to the database server.
     */
    void setPort(unsigned int port);

    /**
     * @brief Retrieves the port of the database connection.
     *
     * @return The port number used to connect to the database server.
     */
    unsigned int port() const noexcept;

    /**
     * @brief Sets the name of the database to connect to.
     *
     * If the database connection is open, it will be closed before setting the new database name.
     *
     * @param databaseName The name of the database.
     */
    void setDatabaseName(std::string databaseName);

    /**
     * @brief Retrieves the name of the database to connect to.
     *
     * @return The name of the database.
     */
    const std::string& databaseName() const noexcept;

    /**
     * @brief Sets the username for the database connection.
     *
     * If the database connection is open, it will be closed before setting the new username.
     *
     * @param userName The username for authentication.
     */
    void setUserName(std::string userName);

    /**
     * @brief Retrieves the username for the database connection.
     *
     * @return The username used for authentication.
     */
    const std::string& userName() const noexcept;

    /**
     * @brief Sets the password for the database connection.
     *
     * If the database connection is open, it will be closed before setting the new password.
     *
     * @param password The password for authentication.
     */
    void setPassword(std::string password);

    /**
     * @brief Retrieves the password for the database connection.
     *
     * @return The password used for authentication.
     */
    const std::string& password() const noexcept;

    /**
     * @brief Sets the connection options for the database.
     *
     * If the database connection is open, it will be closed before setting the new options.
     *
     * @param options The connection options, including hostname, port, database name, username, and password.
     */
    void setConnectionOptions(ConnectionOptions options);

    /**
     * @brief Retrieves the connection options for the database.
     *
     * @return The current connection options.
     */
    const ConnectionOptions& connectionOptions() const noexcept;

    /**
     * @brief Sets the interval for the ping timer.
     *
     * The ping timer is used to periodically check the connection status.
     *
     * @param interval The interval duration for the ping timer. If the interval is negative, it will be set to zero.
     */
    void setPingInterval(Timer::Duration interval);

    /**
     * @brief Retrieves the interval for the ping timer.
     *
     * @return The interval duration for the ping timer.
     */
    Timer::Duration pingInterval() const noexcept;

    /**
     * @brief Sets the feature flags for the database connection.
     *
     * Feature flags control optional behaviors such as automatic reconnection and ping timeouts.
     *
     * @param flags The feature flags to enable.
     */
    void setFeatureFlags(FeatureFlags flags);

    /**
     * @brief Retrieves the current feature flags for the database connection.
     *
     * @return The currently enabled feature flags.
     */
    FeatureFlags featureFlags() const noexcept;

    /**
     * @brief Enables a specific feature flag for the database connection.
     *
     * @param feature The feature flag to enable.
     */
    void enableFeature(FeatureFlags feature);

    /**
     * @brief Disables a specific feature flag for the database connection.
     *
     * @param feature The feature flag to disable.
     */
    void disableFeature(FeatureFlags feature);

    /**
     * @brief Tests if a specific feature flag is enabled.
     *
     * @param feature The feature flag to test.
     * @return True if the feature flag is enabled, false otherwise.
     */
    bool testFeature(FeatureFlag feature) const noexcept;

    /**
     * @brief Opens the database connection.
     *
     * Attempts to establish a connection to the database using the current connection options.
     * If the connection is already open, it will verify the connection status.
     *
     * @return True if the connection is successfully opened, false otherwise.
     */
    bool open() override;

    /**
     * @brief Closes the database connection.
     *
     * Closes the current database connection if it is open.
     */
    void close() override;

    /**
     * @brief Checks if the database connection is open.
     *
     * @return True if the connection is open, false otherwise.
     */
    bool isOpen() const noexcept override;

    /**
     * @brief Sends a ping to the database server.
     *
     * This method checks the connection status by sending a ping to the database server.
     *
     * @return True if the ping is successful, false otherwise.
     */
    bool ping();

    Signal<bool> connectionChanged;
    Signal<std::string> errorOccurred;

protected:
    std::unique_ptr<SqlQueryDriver> createQueryDriver() override;
    void onAboutToMoveToThread(EventLoop* newLoop) override;
    void onMovedToThread(EventLoop* oldLoop) override;

private:
    friend class MySqlQueryDriver;

    void* handle() const noexcept;
    void closeHandle(bool emitSignal);
    void configurePingTimer();
    void onPingTimeout();
    void handleConnectionLost(std::string errorString);
    bool hasFeature(FeatureFlag feature) const noexcept;

private:
    ConnectionOptions m_options;
    void* m_handle = nullptr;
    Timer* m_pingTimer = nullptr;
    Timer::Duration m_pingInterval{std::chrono::milliseconds(30000)};
    FeatureFlags m_featureFlags = NoFeatures;
    bool m_restartPingAfterMove = false;
};

inline MySqlDatabase::FeatureFlags operator|(MySqlDatabase::FeatureFlag lhs, MySqlDatabase::FeatureFlag rhs)
{
    return static_cast<MySqlDatabase::FeatureFlags>(lhs) | static_cast<MySqlDatabase::FeatureFlags>(rhs);
}

}  // namespace snf
