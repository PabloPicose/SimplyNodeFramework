#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFDatabase/MySqlDatabase.h"
#include "SNFDatabase/SqlQuery.h"

#include <cstdlib>
#include <string>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class MySqlDatabaseTest : public ::testing::Test
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

std::string envOr(const char* name, std::string fallback)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? std::string(value) : std::move(fallback);
}

unsigned int envPortOr(const char* name, unsigned int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }

    try {
        return static_cast<unsigned int>(std::stoul(value));
    } catch (...) {
        return fallback;
    }
}

MySqlDatabase::ConnectionOptions testOptions()
{
    MySqlDatabase::ConnectionOptions options;
    options.hostName = envOr("SNF_MYSQL_HOST", "127.0.0.1");
    options.port = envPortOr("SNF_MYSQL_PORT", 3307);
    options.databaseName = envOr("SNF_MYSQL_DATABASE", "test_db");
    options.userName = envOr("SNF_MYSQL_USER", "test_user");
    options.password = envOr("SNF_MYSQL_PASSWORD", "test_pass");
    return options;
}

bool resetSchema(MySqlDatabase& database)
{
    return database.execute("SET FOREIGN_KEY_CHECKS = 0")
        && database.execute("DROP TABLE IF EXISTS orders")
        && database.execute("DROP TABLE IF EXISTS users")
        && database.execute("SET FOREIGN_KEY_CHECKS = 1")
        && database.execute(
            "CREATE TABLE users ("
            "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "email VARCHAR(255) NOT NULL,"
            "name VARCHAR(100) NOT NULL,"
            "status ENUM('active', 'disabled') NOT NULL DEFAULT 'active',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id),"
            "UNIQUE KEY uq_users_email (email),"
            "CHECK (CHAR_LENGTH(name) >= 2)"
            ") ENGINE=InnoDB")
        && database.execute(
            "CREATE TABLE orders ("
            "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "user_id BIGINT UNSIGNED NOT NULL,"
            "reviewed_by_user_id BIGINT UNSIGNED NULL,"
            "order_number VARCHAR(32) NOT NULL,"
            "amount_cents INT UNSIGNED NOT NULL,"
            "status ENUM('pending', 'paid', 'cancelled') NOT NULL DEFAULT 'pending',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id),"
            "UNIQUE KEY uq_orders_order_number (order_number),"
            "KEY idx_orders_user_id (user_id),"
            "KEY idx_orders_reviewed_by_user_id (reviewed_by_user_id),"
            "CONSTRAINT fk_orders_user FOREIGN KEY (user_id) REFERENCES users(id) "
            "ON DELETE CASCADE ON UPDATE RESTRICT,"
            "CONSTRAINT fk_orders_reviewed_by_user FOREIGN KEY (reviewed_by_user_id) REFERENCES users(id) "
            "ON DELETE SET NULL ON UPDATE RESTRICT,"
            "CHECK (amount_cents > 0)"
            ") ENGINE=InnoDB")
        && database.execute(
            "INSERT INTO users (email, name, status) VALUES "
            "('alice@example.com', 'Alice', 'active'),"
            "('bob@example.com', 'Bob', 'active'),"
            "('disabled@example.com', 'Disabled User', 'disabled')")
        && database.execute(
            "INSERT INTO orders (user_id, reviewed_by_user_id, order_number, amount_cents, status) VALUES "
            "(1, 2, 'ORD-0001', 1299, 'pending'),"
            "(1, NULL, 'ORD-0002', 4999, 'paid'),"
            "(2, 1, 'ORD-0003', 2500, 'cancelled')");
}

}  // namespace

TEST_F(MySqlDatabaseTest, OpenUsesStoredConnectionOptionsAndEmitsSignal)
{
    MySqlDatabase database(testOptions());

    int connectedCount = 0;
    database.connectionChanged.connect([&](bool connected) {
        if (connected) {
            ++connectedCount;
        }
    });

    if (! database.open()) {
        GTEST_SKIP() << "MySQL test database is not available: " << database.errorString();
    }

    EXPECT_TRUE(database.isOpen());
    EXPECT_EQ(connectedCount, 1);
}

TEST_F(MySqlDatabaseTest, QueryIteratesSeededRowsAndReportsColumns)
{
    MySqlDatabase database(testOptions());
    if (! database.open()) {
        GTEST_SKIP() << "MySQL test database is not available: " << database.errorString();
    }
    ASSERT_TRUE(resetSchema(database)) << database.errorString();

    SqlQuery query(database);
    ASSERT_TRUE(query.exec(
        "SELECT users.name, orders.order_number, orders.amount_cents "
        "FROM orders INNER JOIN users ON users.id = orders.user_id "
        "ORDER BY orders.order_number"))
        << query.errorString();

    EXPECT_EQ(query.columnCount(), 3);
    EXPECT_EQ(query.columnName(0), "name");
    EXPECT_EQ(query.columnName(1), "order_number");
    EXPECT_EQ(query.columnName(2), "amount_cents");

    ASSERT_TRUE(query.next()) << query.errorString();
    EXPECT_EQ(query.value(0), "Alice");
    EXPECT_EQ(query.value(1), "ORD-0001");
    EXPECT_EQ(query.typedValue(2).toInt64(), 1299);

    ASSERT_TRUE(query.next()) << query.errorString();
    EXPECT_EQ(query.value(1), "ORD-0002");

    ASSERT_TRUE(query.next()) << query.errorString();
    EXPECT_EQ(query.value(0), "Bob");
    EXPECT_FALSE(query.next());
}

TEST_F(MySqlDatabaseTest, QueryReportsNullValues)
{
    MySqlDatabase database(testOptions());
    if (! database.open()) {
        GTEST_SKIP() << "MySQL test database is not available: " << database.errorString();
    }
    ASSERT_TRUE(resetSchema(database)) << database.errorString();

    SqlQuery query(database);
    ASSERT_TRUE(query.exec("SELECT reviewed_by_user_id FROM orders WHERE order_number = 'ORD-0002'"))
        << query.errorString();
    ASSERT_TRUE(query.next()) << query.errorString();

    EXPECT_TRUE(query.isNull(0));
    EXPECT_EQ(query.value(0), "");
}

TEST_F(MySqlDatabaseTest, InvalidSqlReportsQueryError)
{
    MySqlDatabase database(testOptions());
    if (! database.open()) {
        GTEST_SKIP() << "MySQL test database is not available: " << database.errorString();
    }

    SqlQuery query(database);

    EXPECT_FALSE(query.exec("SELCT broken"));
    EXPECT_EQ(query.lastError(), SqlDatabase::DatabaseError::QueryFailed);
    EXPECT_FALSE(query.errorString().empty());
    EXPECT_EQ(database.lastError(), SqlDatabase::DatabaseError::QueryFailed);
}

TEST_F(MySqlDatabaseTest, CloseAndOpenReusesStoredConnectionOptions)
{
    MySqlDatabase database(testOptions());
    if (! database.open()) {
        GTEST_SKIP() << "MySQL test database is not available: " << database.errorString();
    }

    int disconnectedCount = 0;
    database.connectionChanged.connect([&](bool connected) {
        if (! connected) {
            ++disconnectedCount;
        }
    });

    database.close();
    EXPECT_FALSE(database.isOpen());
    EXPECT_EQ(disconnectedCount, 1);

    ASSERT_TRUE(database.open()) << database.errorString();
    EXPECT_TRUE(database.ping()) << database.errorString();
}

TEST_F(MySqlDatabaseTest, FeatureFlagsEnablePingAndAutoReconnect)
{
    MySqlDatabase database(testOptions());
    database.setPingInterval(30ms);
    database.enableFeature(MySqlDatabase::PingTimeout | MySqlDatabase::AutoReconnect);

    EXPECT_EQ(database.pingInterval(), 30ms);
    EXPECT_TRUE(database.testFeature(MySqlDatabase::PingTimeout));
    EXPECT_TRUE(database.testFeature(MySqlDatabase::AutoReconnect));

    if (! database.open()) {
        GTEST_SKIP() << "MySQL test database is not available: " << database.errorString();
    }

    EXPECT_TRUE(database.ping()) << database.errorString();
}

TEST_F(MySqlDatabaseTest, AutoReconnectReopensAfterKilledConnection)
{
    MySqlDatabase database(testOptions());
    database.enableFeature(MySqlDatabase::AutoReconnect);
    if (! database.open()) {
        GTEST_SKIP() << "MySQL test database is not available: " << database.errorString();
    }

    SqlQuery idQuery(database);
    ASSERT_TRUE(idQuery.exec("SELECT CONNECTION_ID()")) << idQuery.errorString();
    ASSERT_TRUE(idQuery.next()) << idQuery.errorString();
    const std::string connectionId = idQuery.value(0);
    idQuery.finish();

    int disconnectedCount = 0;
    int connectedCount = 0;
    database.connectionChanged.connect([&](bool connected) {
        connected ? ++connectedCount : ++disconnectedCount;
    });

    MySqlDatabase killer(testOptions());
    ASSERT_TRUE(killer.open()) << killer.errorString();
    if (! killer.execute("KILL CONNECTION " + connectionId)) {
        GTEST_SKIP() << "Current MySQL user cannot kill its own test connection: " << killer.errorString();
    }

    EXPECT_FALSE(database.ping());
    EXPECT_TRUE(database.isOpen());
    EXPECT_GE(disconnectedCount, 1);
    EXPECT_GE(connectedCount, 1);

    SqlQuery query(database);
    ASSERT_TRUE(query.exec("SELECT 1")) << query.errorString();
    ASSERT_TRUE(query.next()) << query.errorString();
    EXPECT_EQ(query.value(0), "1");
}
