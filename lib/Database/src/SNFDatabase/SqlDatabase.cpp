#include "SNFDatabase/SqlDatabase.h"

#include "SNFDatabase/SqlQuery.h"

#include <utility>

namespace snf {

SqlDatabase::SqlDatabase(Node* parent)
    : Node(parent)
{
}

SqlDatabase::~SqlDatabase() = default;

bool SqlDatabase::execute(const std::string& sql)
{
    SqlQuery query(*this);
    if (! query.exec(sql)) {
        setError(query.lastError(), query.errorString());
        return false;
    }

    clearError();
    return true;
}

SqlDatabase::DatabaseError SqlDatabase::lastError() const noexcept
{
    return m_lastError;
}

const std::string& SqlDatabase::errorString() const noexcept
{
    return m_errorString;
}

void SqlDatabase::update()
{
}

void SqlDatabase::clearError() const
{
    m_lastError = DatabaseError::NoError;
    m_errorString.clear();
}

void SqlDatabase::setError(DatabaseError error, std::string errorString) const
{
    m_lastError = error;
    m_errorString = std::move(errorString);
}

}  // namespace snf
