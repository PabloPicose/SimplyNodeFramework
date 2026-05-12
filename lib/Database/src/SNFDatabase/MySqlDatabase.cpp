#include "SNFDatabase/MySqlDatabase.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"
#include "SNFDatabase/SqlQuery.h"

#include <mysql/mysql.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace snf {

namespace {

bool validColumn(MYSQL_RES* result, MYSQL_ROW row, int column)
{
    return result != nullptr && row != nullptr && column >= 0
        && column < static_cast<int>(mysql_num_fields(result));
}

std::string mysqlError(MYSQL* handle, const char* fallback)
{
    if (handle == nullptr) {
        return fallback != nullptr ? std::string(fallback) : std::string();
    }

    const char* message = mysql_error(handle);
    return message != nullptr && message[0] != '\0'
        ? std::string(message)
        : std::string(fallback != nullptr ? fallback : "MySQL error");
}

bool isConnectionError(unsigned int error)
{
    return error == CR_SERVER_GONE_ERROR || error == CR_SERVER_LOST || error == CR_CONN_HOST_ERROR
        || error == CR_CONNECTION_ERROR;
}

Variant valueToVariant(enum enum_field_types type, const char* data)
{
    if (data == nullptr) {
        return {};
    }

    switch (type) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_YEAR: {
            char* end = nullptr;
            errno = 0;
            const long long parsed = std::strtoll(data, &end, 10);
            if (errno == 0 && end != data) {
                return static_cast<std::int64_t>(parsed);
            }
            return std::string(data);
        }
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL: {
            char* end = nullptr;
            errno = 0;
            const double parsed = std::strtod(data, &end);
            if (errno == 0 && end != data) {
                return parsed;
            }
            return std::string(data);
        }
        case MYSQL_TYPE_BIT:
            return std::string(data) == "1";
        default:
            return std::string(data);
    }
}

}  // namespace

class MySqlQueryDriver final : public SqlQueryDriver
{
public:
    explicit MySqlQueryDriver(MySqlDatabase& database)
        : m_database(database)
    {
    }

    ~MySqlQueryDriver() override { finish(); }

    bool exec(const std::string& sql) override
    {
        finish();
        clearError();

        MYSQL* database = m_database.handle();
        if (database == nullptr) {
            setError(SqlDatabase::DatabaseError::InvalidOperation, "MySQL database is not open");
            return false;
        }

        if (mysql_query(database, sql.c_str()) != 0) {
            const unsigned int error = mysql_errno(database);
            const std::string message = mysqlError(database, "MySQL query failed");
            setError(SqlDatabase::DatabaseError::QueryFailed, message);
            if (isConnectionError(error)) {
                m_database.handleConnectionLost(message);
            }
            return false;
        }

        if (mysql_field_count(database) == 0) {
            return true;
        }

        m_result = mysql_store_result(database);
        if (m_result == nullptr) {
            const unsigned int error = mysql_errno(database);
            const std::string message = mysqlError(database, "Failed to store MySQL result");
            setError(SqlDatabase::DatabaseError::QueryFailed, message);
            if (isConnectionError(error)) {
                m_database.handleConnectionLost(message);
            }
            return false;
        }

        return true;
    }

    bool next() override
    {
        clearError();

        if (m_result == nullptr) {
            return false;
        }

        m_row = mysql_fetch_row(m_result);
        if (m_row != nullptr) {
            m_lengths = mysql_fetch_lengths(m_result);
            return true;
        }

        MYSQL* database = m_database.handle();
        if (database != nullptr && mysql_errno(database) != 0) {
            const unsigned int error = mysql_errno(database);
            const std::string message = mysqlError(database, "Failed to fetch MySQL row");
            setError(SqlDatabase::DatabaseError::QueryFailed, message);
            if (isConnectionError(error)) {
                m_database.handleConnectionLost(message);
            }
        }

        finish();
        return false;
    }

    void finish() override
    {
        if (m_result != nullptr) {
            mysql_free_result(m_result);
            m_result = nullptr;
        }
        m_row = nullptr;
        m_lengths = nullptr;
    }

    int columnCount() const override
    {
        return m_result != nullptr ? static_cast<int>(mysql_num_fields(m_result)) : 0;
    }

    std::string columnName(int column) const override
    {
        if (m_result == nullptr || column < 0 || column >= columnCount()) {
            return {};
        }

        MYSQL_FIELD* fields = mysql_fetch_fields(m_result);
        return fields != nullptr && fields[column].name != nullptr
            ? std::string(fields[column].name)
            : std::string();
    }

    std::string value(int column) const override
    {
        if (! validColumn(m_result, m_row, column) || isNull(column)) {
            return {};
        }

        const unsigned long length = m_lengths != nullptr ? m_lengths[column] : std::strlen(m_row[column]);
        return std::string(m_row[column], length);
    }

    Variant typedValue(int column) const override
    {
        if (! validColumn(m_result, m_row, column) || isNull(column)) {
            return {};
        }

        MYSQL_FIELD* fields = mysql_fetch_fields(m_result);
        if (fields == nullptr) {
            return value(column);
        }

        return valueToVariant(fields[column].type, value(column).c_str());
    }

    bool isNull(int column) const override
    {
        return ! validColumn(m_result, m_row, column) || m_row[column] == nullptr;
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
    MySqlDatabase& m_database;
    MYSQL_RES* m_result = nullptr;
    MYSQL_ROW m_row = nullptr;
    unsigned long* m_lengths = nullptr;
    SqlDatabase::DatabaseError m_lastError = SqlDatabase::DatabaseError::NoError;
    std::string m_errorString;
};

MySqlDatabase::MySqlDatabase(Node* parent)
    : SqlDatabase(parent)
{
    m_pingTimer = new Timer(this);
    m_pingTimer->timeout.connect(NodePtr<MySqlDatabase>(this), &MySqlDatabase::onPingTimeout);
}

MySqlDatabase::MySqlDatabase(ConnectionOptions options, Node* parent)
    : SqlDatabase(parent)
    , m_options(std::move(options))
{
    m_pingTimer = new Timer(this);
    m_pingTimer->timeout.connect(NodePtr<MySqlDatabase>(this), &MySqlDatabase::onPingTimeout);
}

MySqlDatabase::~MySqlDatabase()
{
    if (m_pingTimer != nullptr) {
        m_pingTimer->stop();
    }
    closeHandle(false);
}

void MySqlDatabase::setHostName(std::string hostName)
{
    if (isOpen()) {
        close();
    }
    m_options.hostName = std::move(hostName);
    clearError();
}

const std::string& MySqlDatabase::hostName() const noexcept
{
    return m_options.hostName;
}

void MySqlDatabase::setPort(unsigned int port)
{
    if (isOpen()) {
        close();
    }
    m_options.port = port;
    clearError();
}

unsigned int MySqlDatabase::port() const noexcept
{
    return m_options.port;
}

void MySqlDatabase::setDatabaseName(std::string databaseName)
{
    if (isOpen()) {
        close();
    }
    m_options.databaseName = std::move(databaseName);
    clearError();
}

const std::string& MySqlDatabase::databaseName() const noexcept
{
    return m_options.databaseName;
}

void MySqlDatabase::setUserName(std::string userName)
{
    if (isOpen()) {
        close();
    }
    m_options.userName = std::move(userName);
    clearError();
}

const std::string& MySqlDatabase::userName() const noexcept
{
    return m_options.userName;
}

void MySqlDatabase::setPassword(std::string password)
{
    if (isOpen()) {
        close();
    }
    m_options.password = std::move(password);
    clearError();
}

const std::string& MySqlDatabase::password() const noexcept
{
    return m_options.password;
}

void MySqlDatabase::setConnectionOptions(ConnectionOptions options)
{
    if (isOpen()) {
        close();
    }
    m_options = std::move(options);
    clearError();
}

const MySqlDatabase::ConnectionOptions& MySqlDatabase::connectionOptions() const noexcept
{
    return m_options;
}

void MySqlDatabase::setPingInterval(Timer::Duration interval)
{
    if (interval < Timer::Duration::zero()) {
        interval = Timer::Duration::zero();
    }

    m_pingInterval = interval;
    configurePingTimer();
}

Timer::Duration MySqlDatabase::pingInterval() const noexcept
{
    return m_pingInterval;
}

void MySqlDatabase::setFeatureFlags(FeatureFlags flags)
{
    m_featureFlags = flags;
    configurePingTimer();
}

MySqlDatabase::FeatureFlags MySqlDatabase::featureFlags() const noexcept
{
    return m_featureFlags;
}

void MySqlDatabase::enableFeature(FeatureFlags feature)
{
    setFeatureFlags(m_featureFlags | feature);
}

void MySqlDatabase::disableFeature(FeatureFlags feature)
{
    setFeatureFlags(m_featureFlags & ~feature);
}

bool MySqlDatabase::testFeature(FeatureFlag feature) const noexcept
{
    return hasFeature(feature);
}

bool MySqlDatabase::open()
{
    if (isOpen()) {
        MYSQL* database = handle();
        if (database != nullptr && mysql_ping(database) == 0) {
            clearError();
            configurePingTimer();
            return true;
        }

        const std::string message = mysqlError(database, "MySQL ping failed");
        setError(DatabaseError::OpenFailed, message);
        closeHandle(true);
    }

    closeHandle(false);

    if (m_options.databaseName.empty()) {
        setError(DatabaseError::OpenFailed, "MySQL database name is empty");
        return false;
    }

    MYSQL* database = mysql_init(nullptr);
    if (database == nullptr) {
        setError(DatabaseError::OpenFailed, "Failed to initialize MySQL connection");
        return false;
    }

    const bool reconnect = false;
    mysql_options(database, MYSQL_OPT_RECONNECT, &reconnect);

    if (mysql_real_connect(database,
                           m_options.hostName.c_str(),
                           m_options.userName.c_str(),
                           m_options.password.c_str(),
                           m_options.databaseName.c_str(),
                           m_options.port,
                           nullptr,
                           0)
        == nullptr)
    {
        const std::string message = mysqlError(database, "Failed to open MySQL database");
        mysql_close(database);
        setError(DatabaseError::OpenFailed, message);
        errorOccurred.emit(message);
        return false;
    }

    mysql_set_character_set(database, "utf8mb4");

    m_handle = database;
    clearError();
    connectionChanged.emit(true);
    configurePingTimer();
    return true;
}

void MySqlDatabase::close()
{
    if (m_pingTimer != nullptr) {
        m_pingTimer->stop();
    }
    closeHandle(true);
}

bool MySqlDatabase::isOpen() const noexcept
{
    return m_handle != nullptr;
}

bool MySqlDatabase::ping()
{
    MYSQL* database = handle();
    if (database == nullptr) {
        setError(DatabaseError::InvalidOperation, "MySQL database is not open");
        return false;
    }

    if (mysql_ping(database) == 0) {
        clearError();
        return true;
    }

    const std::string message = mysqlError(database, "MySQL ping failed");
    setError(DatabaseError::OpenFailed, message);
    handleConnectionLost(message);
    return false;
}

std::unique_ptr<SqlQueryDriver> MySqlDatabase::createQueryDriver()
{
    return std::make_unique<MySqlQueryDriver>(*this);
}

void MySqlDatabase::onAboutToMoveToThread(EventLoop* /*newLoop*/)
{
    m_restartPingAfterMove = m_pingTimer != nullptr && m_pingTimer->isActive();
    if (m_pingTimer != nullptr) {
        m_pingTimer->stop();
    }
}

void MySqlDatabase::onMovedToThread(EventLoop* /*oldLoop*/)
{
    if (! m_restartPingAfterMove) {
        return;
    }

    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<MySqlDatabase>(this)]() {
            if (self) {
                self->configurePingTimer();
            }
        });
    }
}

MYSQL* MySqlDatabase::handle() const noexcept
{
    return m_handle;
}

void MySqlDatabase::closeHandle(bool emitSignal)
{
    MYSQL* database = m_handle;
    m_handle = nullptr;

    if (database != nullptr) {
        mysql_close(database);
        if (emitSignal) {
            connectionChanged.emit(false);
        }
    }
}

void MySqlDatabase::configurePingTimer()
{
    if (m_pingTimer == nullptr) {
        return;
    }

    if (! isOpen() || ! hasFeature(PingTimeout)) {
        m_pingTimer->stop();
        return;
    }

    m_pingTimer->setInterval(m_pingInterval);
    if (! m_pingTimer->isActive()) {
        m_pingTimer->start();
    }
}

void MySqlDatabase::onPingTimeout()
{
    if (! hasFeature(PingTimeout) || ! isOpen()) {
        configurePingTimer();
        return;
    }

    ping();
}

void MySqlDatabase::handleConnectionLost(std::string errorString)
{
    const bool wasOpen = isOpen();
    if (m_pingTimer != nullptr) {
        m_pingTimer->stop();
    }
    closeHandle(wasOpen);
    errorOccurred.emit(errorString);

    if (hasFeature(AutoReconnect)) {
        open();
    }
}

bool MySqlDatabase::hasFeature(FeatureFlag feature) const noexcept
{
    return (m_featureFlags & static_cast<FeatureFlags>(feature)) != 0;
}

}  // namespace snf
