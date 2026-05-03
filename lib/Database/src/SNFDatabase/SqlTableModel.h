#pragma once

/**
 * @file SqlTableModel.h
 * @brief Table model that loads and edits a single SQL database table.
 * @ingroup SNFDatabase
 */

#include "SNFCore/AbstractTableModel.h"
#include "SNFDatabase/SqlDatabase.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace snf {

/**
 * @class SqlTableModel
 * @ingroup SNFDatabase
 * @brief AbstractTableModel backed by a single SQL database table.
 *
 * Loads all rows of a table via `select()` and caches them in memory.
 * Edits are buffered locally until `submit()` is called, which writes
 * each modified row as an UPDATE statement. `revert()` discards all
 * pending edits without touching the database.
 *
 * A primary key column is required to build the WHERE clause for UPDATE
 * statements. By default the first column is used; set it explicitly with
 * `setPrimaryKeyColumn()`.
 *
 * @code
 * SqliteDatabase db;
 * db.setDatabaseName("mydb.sqlite");
 * db.open();
 *
 * SqlTableModel model(db);
 * model.setTable("users");
 * model.setPrimaryKeyColumn("id");
 * model.select();
 *
 * // Edit a cell in the view, then:
 * if (!model.submit()) {
 *     std::cerr << model.errorString() << '\n';
 * }
 * @endcode
 */
class SqlTableModel : public AbstractTableModel
{
public:
    /**
     * @brief Constructs the model with the given database connection.
     *
     * The database must outlive the model. No data is loaded until
     * `select()` is called.
     */
    explicit SqlTableModel(SqlDatabase& database);

    /** @brief Sets the name of the table to load. */
    void setTable(const std::string& tableName);

    /** @brief Returns the table name. */
    const std::string& tableName() const noexcept;

    /**
     * @brief Sets the column whose value is used in the WHERE clause of UPDATE
     * statements.
     *
     * If not set, the first column is used by default.
     */
    void setPrimaryKeyColumn(const std::string& columnName);

    /** @brief Returns the primary key column name, or empty if unset. */
    const std::string& primaryKeyColumn() const noexcept;

    /**
     * @brief Loads all rows from the table via `SELECT * FROM tableName`.
     *
     * Clears any pending edits. Emits `modelReset` on success.
     * @return @c true on success, @c false on query error.
     */
    bool select();

    /**
     * @brief Writes all pending edits to the database as UPDATE statements.
     *
     * Each modified row generates one `UPDATE … SET … WHERE pkCol = pkVal`
     * query. On the first failure the method stops, updates the error state,
     * and returns @c false. Successfully committed rows are applied to the
     * in-memory cache and removed from the pending set so a subsequent
     * `submit()` retries only the remaining changes.
     *
     * @return @c true if all pending edits were committed successfully.
     */
    bool submit();

    /**
     * @brief Discards all pending edits without touching the database.
     *
     * Emits `dataChanged` for every cell that was buffered.
     */
    void revert();

    /** @brief Returns @c true if there are uncommitted edits. */
    bool isDirty() const noexcept;

    /** @brief Returns the last database error code. */
    SqlDatabase::DatabaseError lastError() const noexcept;

    /** @brief Returns the human-readable error message for the last error. */
    const std::string& errorString() const noexcept;

    // AbstractTableModel interface
    int rowCount() const override;
    int columnCount() const override;
    std::string data(int row, int column) const override;
    Variant data(const ModelIndex& index,
                    ModelDataRole role = ModelDataRole::Display) const override;
    std::string headerData(int section) const override;
    bool isEditable(int row, int column) const override;
    bool setData(int row, int column, const std::string& value) override;
    bool setData(const ModelIndex& index,
                 const Variant& value,
                 ModelDataRole role = ModelDataRole::Edit) override;

private:
    void clearError() const;
    void setError(SqlDatabase::DatabaseError error, std::string errorString) const;
    static std::string variantToSqlLiteral(const Variant& value);
    int primaryKeyColumnIndex() const;
    const Variant& cachedValue(int row, int column) const;

    SqlDatabase* m_database;
    std::string m_tableName;
    std::string m_primaryKeyColumn;

    std::vector<std::string> m_columnNames;
    std::vector<std::vector<Variant>> m_rows;

    /// Pending edits: m_pendingEdits[row][column] = new Variant.
    std::unordered_map<int, std::unordered_map<int, Variant>> m_pendingEdits;

    mutable SqlDatabase::DatabaseError m_lastError = SqlDatabase::DatabaseError::NoError;
    mutable std::string m_errorString;
};

}  // namespace snf
