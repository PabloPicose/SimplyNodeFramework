#include "SNFCore/AbstractTableModel.h"

#include <sstream>
#include <type_traits>

namespace snf {

AbstractTableModel::~AbstractTableModel() = default;

std::string modelValueToString(const ModelValue& value)
{
    return std::visit(
        [](const auto& current) -> std::string {
            using ValueT = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<ValueT, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<ValueT, bool>) {
                return current ? "true" : "false";
            } else if constexpr (std::is_same_v<ValueT, std::string>) {
                return current;
            } else if constexpr (std::is_same_v<ValueT, ModelColor>) {
                return {};
            } else if constexpr (std::is_same_v<ValueT, double>) {
                std::ostringstream stream;
                stream << current;
                return stream.str();
            } else {
                return std::to_string(current);
            }
        },
        value);
}

ModelIndex AbstractTableModel::index(int row, int column) const
{
    const ModelIndex result(row, column, this);
    return result.isValid() ? result : ModelIndex();
}

ModelValue AbstractTableModel::data(const ModelIndex& index, ModelDataRole role) const
{
    if (! index.isValid() || index.model() != this) {
        return std::monostate{};
    }

    if (role == ModelDataRole::Decoration) {
        return std::monostate{};
    }

    if (role != ModelDataRole::Display && role != ModelDataRole::Edit) {
        return std::monostate{};
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

bool AbstractTableModel::setData(const ModelIndex& index, const ModelValue& value, ModelDataRole role)
{
    if (role != ModelDataRole::Edit || ! index.isValid() || index.model() != this) {
        return false;
    }

    return setData(index.row(), index.column(), modelValueToString(value));
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
