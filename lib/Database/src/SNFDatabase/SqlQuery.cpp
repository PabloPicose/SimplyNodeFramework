#include "SNFDatabase/SqlQuery.h"

#include <utility>

namespace snf {

SqlQueryDriver::~SqlQueryDriver() = default;

SqlQuery::SqlQuery(SqlDatabase& database)
    : m_database(&database)
    , m_driver(database.createQueryDriver())
{
    if (! m_driver) {
        setError(SqlDatabase::DatabaseError::InvalidOperation, "Database did not create a query driver");
    }
}

SqlQuery::~SqlQuery() = default;

SqlQuery::SqlQuery(SqlQuery&&) noexcept = default;

SqlQuery& SqlQuery::operator=(SqlQuery&&) noexcept = default;

bool SqlQuery::exec(const std::string& sql)
{
    if (! m_driver) {
        setError(SqlDatabase::DatabaseError::InvalidOperation, "Query has no database driver");
        return false;
    }

    if (! m_driver->exec(sql)) {
        setError(m_driver->lastError(), m_driver->errorString());
        return false;
    }

    clearError();
    return true;
}

bool SqlQuery::next()
{
    if (! m_driver) {
        setError(SqlDatabase::DatabaseError::InvalidOperation, "Query has no database driver");
        return false;
    }

    const bool hasRow = m_driver->next();
    if (! hasRow && m_driver->lastError() != SqlDatabase::DatabaseError::NoError) {
        setError(m_driver->lastError(), m_driver->errorString());
    }

    return hasRow;
}

void SqlQuery::finish()
{
    if (m_driver) {
        m_driver->finish();
    }
}

int SqlQuery::columnCount() const
{
    return m_driver ? m_driver->columnCount() : 0;
}

std::string SqlQuery::columnName(int column) const
{
    return m_driver ? m_driver->columnName(column) : std::string();
}

std::string SqlQuery::value(int column) const
{
    return m_driver ? m_driver->value(column) : std::string();
}

Variant SqlQuery::typedValue(int column) const
{
    return m_driver ? m_driver->typedValue(column) : Variant{};
}

bool SqlQuery::isNull(int column) const
{
    return m_driver ? m_driver->isNull(column) : true;
}

SqlDatabase::DatabaseError SqlQuery::lastError() const noexcept
{
    if (m_driver && m_lastError == SqlDatabase::DatabaseError::NoError) {
        return m_driver->lastError();
    }

    return m_lastError;
}

const std::string& SqlQuery::errorString() const noexcept
{
    if (m_driver && m_lastError == SqlDatabase::DatabaseError::NoError) {
        return m_driver->errorString();
    }

    return m_errorString;
}

void SqlQuery::clearError()
{
    m_lastError = SqlDatabase::DatabaseError::NoError;
    m_errorString.clear();
    if (m_database) {
        m_database->clearError();
    }
}

void SqlQuery::setError(SqlDatabase::DatabaseError error, std::string errorString)
{
    m_lastError = error;
    m_errorString = std::move(errorString);
    if (m_database) {
        m_database->setError(m_lastError, m_errorString);
    }
}

}  // namespace snf
