#include "SNFCore/AbstractTableModel.h"

namespace snf {

AbstractTableModel::~AbstractTableModel() = default;

ModelIndex AbstractTableModel::index(int row, int column) const
{
    const ModelIndex result(row, column, this);
    return result.isValid() ? result : ModelIndex();
}

Variant AbstractTableModel::data(const ModelIndex& index, ModelDataRole role) const
{
    if (! index.isValid() || index.model() != this) {
        return {};
    }

    if (role == ModelDataRole::Decoration) {
        return {};
    }

    if (role != ModelDataRole::Display && role != ModelDataRole::Edit) {
        return {};
    }

    return data(index.row(), index.column());
}

std::string AbstractTableModel::headerData(int /*section*/) const
{
    return {};
}

std::string AbstractTableModel::verticalHeaderData(int /*section*/) const
{
    return {};
}

bool AbstractTableModel::isEditable(int /*row*/, int /*column*/) const
{
    return false;
}

bool AbstractTableModel::isEditable(const ModelIndex& index) const
{
    return index.isValid() && index.model() == this && isEditable(index.row(), index.column());
}

bool AbstractTableModel::setData(int /*row*/, int /*column*/, const std::string& /*value*/)
{
    return false;
}

bool AbstractTableModel::setData(const ModelIndex& index, const Variant& value, ModelDataRole role)
{
    if (role != ModelDataRole::Edit || ! index.isValid() || index.model() != this) {
        return false;
    }

    return setData(index.row(), index.column(), value.toString());
}

bool AbstractTableModel::insertRows(int /*row*/, int /*count*/)
{
    return false;
}

bool AbstractTableModel::removeRows(int /*row*/, int /*count*/)
{
    return false;
}

bool AbstractTableModel::insertColumns(int /*column*/, int /*count*/)
{
    return false;
}

bool AbstractTableModel::removeColumns(int /*column*/, int /*count*/)
{
    return false;
}

void AbstractTableModel::notifyDataChanged(int row, int column)
{
    dataChanged.emit(row, column);
}

void AbstractTableModel::notifyModelReset()
{
    modelReset.emit();
}

void AbstractTableModel::notifyRowsInserted(int first, int count)
{
    rowsInserted.emit(first, count);
}

void AbstractTableModel::notifyRowsRemoved(int first, int count)
{
    rowsRemoved.emit(first, count);
}

void AbstractTableModel::notifyColumnsInserted(int first, int count)
{
    columnsInserted.emit(first, count);
}

void AbstractTableModel::notifyColumnsRemoved(int first, int count)
{
    columnsRemoved.emit(first, count);
}

}  // namespace snf
