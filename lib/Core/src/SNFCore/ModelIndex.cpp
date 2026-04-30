#include "SNFCore/ModelIndex.h"

#include "SNFCore/AbstractTableModel.h"

namespace snf {

ModelIndex::ModelIndex(int row, int column, const AbstractTableModel* model)
    : m_row(row), m_column(column), m_model(model)
{
}

int ModelIndex::row() const noexcept
{
    return m_row;
}

int ModelIndex::column() const noexcept
{
    return m_column;
}

const AbstractTableModel* ModelIndex::model() const noexcept
{
    return m_model;
}

bool ModelIndex::isValid() const
{
    return m_model && m_row >= 0 && m_column >= 0 && m_row < m_model->rowCount() && m_column < m_model->columnCount();
}

}  // namespace snf
