#include "SNFCore/AbstractTableModel.h"

namespace snf {

AbstractTableModel::~AbstractTableModel() = default;

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

bool AbstractTableModel::setData(int /*row*/, int /*column*/, const std::string& /*value*/)
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
