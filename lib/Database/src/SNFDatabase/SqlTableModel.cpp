#include "SNFDatabase/SqlTableModel.h"

#include "SNFDatabase/SqlQuery.h"

#include <sstream>
#include <utility>

namespace snf {

SqlTableModel::SqlTableModel(SqlDatabase& database)
    : m_database(&database)
{
}

SqlTableModel::SqlTableModel(SqlDatabaseProvider& databaseProvider)
    : m_databaseProvider(&databaseProvider)
{
}

SqlDatabase& SqlTableModel::database() const
{
    if (m_databaseProvider != nullptr) {
        return m_databaseProvider->database();
    }

    return *m_database;
}

void SqlTableModel::setTable(const std::string& tableName)
{
    m_tableName = tableName;
}

const std::string& SqlTableModel::tableName() const noexcept
{
    return m_tableName;
}

void SqlTableModel::setPrimaryKeyColumn(const std::string& columnName)
{
    m_primaryKeyColumn = columnName;
}

const std::string& SqlTableModel::primaryKeyColumn() const noexcept
{
    return m_primaryKeyColumn;
}

bool SqlTableModel::select()
{
    if (m_tableName.empty()) {
        setError(SqlDatabase::DatabaseError::InvalidOperation, "No table name set");
        return false;
    }

    SqlQuery query(database());
    if (! query.exec("SELECT * FROM " + m_tableName)) {
        setError(query.lastError(), query.errorString());
        return false;
    }

    const int colCount = query.columnCount();

    std::vector<std::string> newColumnNames;
    newColumnNames.reserve(colCount);
    for (int i = 0; i < colCount; ++i) {
        newColumnNames.push_back(query.columnName(i));
    }

    std::vector<std::vector<Variant>> newRows;
    while (query.next()) {
        std::vector<Variant> row;
        row.reserve(colCount);
        for (int i = 0; i < colCount; ++i) {
            row.push_back(query.typedValue(i));
        }
        newRows.push_back(std::move(row));
    }

    if (query.lastError() != SqlDatabase::DatabaseError::NoError) {
        setError(query.lastError(), query.errorString());
        return false;
    }

    m_columnNames = std::move(newColumnNames);
    m_rows = std::move(newRows);
    m_pendingEdits.clear();
    clearError();
    modelReset.emit();
    return true;
}

bool SqlTableModel::submit()
{
    if (m_pendingEdits.empty()) {
        return true;
    }

    if (m_tableName.empty()) {
        setError(SqlDatabase::DatabaseError::InvalidOperation, "No table name set");
        return false;
    }

    const int pkIndex = primaryKeyColumnIndex();
    if (pkIndex < 0) {
        setError(
            SqlDatabase::DatabaseError::InvalidOperation,
            m_primaryKeyColumn.empty()
                ? "Cannot submit: the table has no columns"
                : "Primary key column '" + m_primaryKeyColumn + "' not found in table '" + m_tableName + "'"
        );
        return false;
    }

    bool success = true;
    std::unordered_map<int, std::unordered_map<int, Variant>> failedEdits;

    for (auto& [row, edits] : m_pendingEdits) {
        if (! success) {
            failedEdits.insert({row, edits});
            continue;
        }

        if (row < 0 || row >= static_cast<int>(m_rows.size())) {
            continue;
        }

        std::string setClauses;
        bool firstClause = true;
        for (auto& [col, newVal] : edits) {
            if (col < 0 || col >= static_cast<int>(m_columnNames.size())) {
                continue;
            }
            if (! firstClause) {
                setClauses += ", ";
            }
            setClauses += m_columnNames[col] + " = " + variantToSqlLiteral(newVal);
            firstClause = false;
        }

        if (setClauses.empty()) {
            continue;
        }

        const std::string sql =
            "UPDATE " + m_tableName
            + " SET " + setClauses
            + " WHERE " + m_columnNames[pkIndex] + " = " + variantToSqlLiteral(m_rows[row][pkIndex]);

        SqlQuery query(database());
        if (! query.exec(sql)) {
            setError(query.lastError(), query.errorString());
            failedEdits.insert({row, edits});
            success = false;
            continue;
        }

        for (auto& [col, newVal] : edits) {
            if (col >= 0 && col < static_cast<int>(m_rows[row].size())) {
                m_rows[row][col] = newVal;
            }
        }
    }

    m_pendingEdits = std::move(failedEdits);

    if (! success) {
        return false;
    }

    clearError();
    return true;
}

void SqlTableModel::revert()
{
    if (m_pendingEdits.empty()) {
        return;
    }

    std::vector<std::pair<int, int>> dirtyCells;
    for (auto& [row, edits] : m_pendingEdits) {
        for (auto& [col, val] : edits) {
            dirtyCells.emplace_back(row, col);
        }
    }

    m_pendingEdits.clear();

    for (auto& [row, col] : dirtyCells) {
        dataChanged.emit(row, col);
    }
}

bool SqlTableModel::isDirty() const noexcept
{
    return ! m_pendingEdits.empty();
}

SqlDatabase::DatabaseError SqlTableModel::lastError() const noexcept
{
    return m_lastError;
}

const std::string& SqlTableModel::errorString() const noexcept
{
    return m_errorString;
}

int SqlTableModel::rowCount() const
{
    return static_cast<int>(m_rows.size());
}

int SqlTableModel::columnCount() const
{
    return static_cast<int>(m_columnNames.size());
}

const Variant& SqlTableModel::cachedValue(int row, int column) const
{
    auto rowIt = m_pendingEdits.find(row);
    if (rowIt != m_pendingEdits.end()) {
        auto colIt = rowIt->second.find(column);
        if (colIt != rowIt->second.end()) {
            return colIt->second;
        }
    }
    return m_rows[row][column];
}

std::string SqlTableModel::data(int row, int column) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size())
        || column < 0 || column >= static_cast<int>(m_columnNames.size()))
    {
        return {};
    }
    return cachedValue(row, column).toString();
}

Variant SqlTableModel::data(const ModelIndex& index, ModelDataRole role) const
{
    if (! index.isValid() || index.model() != this) {
        return {};
    }

    if (role == ModelDataRole::Decoration) {
        return {};
    }

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= static_cast<int>(m_rows.size())
        || col < 0 || col >= static_cast<int>(m_columnNames.size()))
    {
        return {};
    }

    return cachedValue(row, col);
}

std::string SqlTableModel::headerData(int section) const
{
    if (section < 0 || section >= static_cast<int>(m_columnNames.size())) {
        return {};
    }
    return m_columnNames[section];
}

bool SqlTableModel::isEditable(int row, int column) const
{
    return row >= 0 && row < static_cast<int>(m_rows.size())
        && column >= 0 && column < static_cast<int>(m_columnNames.size());
}

bool SqlTableModel::setData(int row, int column, const std::string& value)
{
    return setData(index(row, column), Variant{value}, ModelDataRole::Edit);
}

bool SqlTableModel::setData(const ModelIndex& idx, const Variant& value, ModelDataRole role)
{
    if (role == ModelDataRole::Decoration) {
        return false;
    }

    const int row = idx.row();
    const int col = idx.column();

    if (! isEditable(row, col)) {
        return false;
    }

    if (cachedValue(row, col) == value) {
        return true;
    }

    m_pendingEdits[row][col] = value;
    dataChanged.emit(row, col);
    return true;
}

void SqlTableModel::clearError() const
{
    m_lastError = SqlDatabase::DatabaseError::NoError;
    m_errorString.clear();
}

void SqlTableModel::setError(SqlDatabase::DatabaseError error, std::string errorString) const
{
    m_lastError = error;
    m_errorString = std::move(errorString);
}

std::string SqlTableModel::variantToSqlLiteral(const Variant& value)
{
    switch (value.type()) {
        case Variant::Type::Null:
            return "NULL";
        case Variant::Type::Bool:
            return value.toBool() ? "1" : "0";
        case Variant::Type::Int64:
            return std::to_string(value.toInt64());
        case Variant::Type::Double: {
            std::ostringstream os;
            os << value.toDouble();
            return os.str();
        }
        case Variant::Type::String: {
            const std::string& s = *value.get<std::string>();
            std::string escaped;
            escaped.reserve(s.size() + 2);
            for (char c : s) {
                if (c == '\'')
                    escaped += "''";
                else
                    escaped += c;
            }
            return "'" + escaped + "'";
        }
        case Variant::Type::Color:
            return "NULL";
    }
    return "NULL";
}

int SqlTableModel::primaryKeyColumnIndex() const
{
    if (m_primaryKeyColumn.empty()) {
        return m_columnNames.empty() ? -1 : 0;
    }
    for (int i = 0; i < static_cast<int>(m_columnNames.size()); ++i) {
        if (m_columnNames[i] == m_primaryKeyColumn) {
            return i;
        }
    }
    return -1;
}

}  // namespace snf
