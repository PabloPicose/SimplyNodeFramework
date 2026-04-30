#pragma once

/**
 * @file ModelIndex.h
 * @brief Lightweight table-model index.
 * @ingroup SNFCore
 */

namespace snf {

class AbstractTableModel;

/**
 * @class ModelIndex
 * @ingroup SNFCore
 * @brief Identifies a cell inside an `AbstractTableModel`.
 *
 * A default-constructed index is invalid. `ModelIndex` is intentionally small
 * and non-owning; it does not keep a model alive.
 */
class ModelIndex
{
public:
    ModelIndex() = default;
    ModelIndex(int row, int column, const AbstractTableModel* model);

    int row() const noexcept;
    int column() const noexcept;
    const AbstractTableModel* model() const noexcept;
    bool isValid() const;

    friend bool operator==(const ModelIndex& lhs, const ModelIndex& rhs) noexcept
    {
        return lhs.m_row == rhs.m_row && lhs.m_column == rhs.m_column && lhs.m_model == rhs.m_model;
    }

    friend bool operator!=(const ModelIndex& lhs, const ModelIndex& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    int m_row = -1;
    int m_column = -1;
    const AbstractTableModel* m_model = nullptr;
};

}  // namespace snf
