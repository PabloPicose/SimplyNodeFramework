#include "SNFWidgets/DataWidgetMapper.h"

#include "SNFWidgets/LineEdit.h"

#include <SNFCore/AbstractTableModel.h>

#include <algorithm>
#include <string>
#include <variant>

namespace snf {
namespace widgets {

DataWidgetMapper::DataWidgetMapper() = default;

DataWidgetMapper::~DataWidgetMapper()
{
    disconnectModelSignals();
}

void DataWidgetMapper::setModel(snf::AbstractTableModel* model)
{
    if (m_model == model) {
        return;
    }

    disconnectModelSignals();
    m_model = model;
    connectModelSignals();
    setCurrentRowInternal(-1, true);
}

snf::AbstractTableModel* DataWidgetMapper::model() const
{
    return m_model;
}

void DataWidgetMapper::addMapping(LineEdit* widget, int column, snf::ModelDataRole role)
{
    if (! widget) {
        return;
    }

    for (Mapping& mapping : m_mappings) {
        if (mapping.widget == widget) {
            mapping.column = column;
            mapping.role = role;
            refreshMapping(mapping);
            return;
        }
    }

    m_mappings.push_back({snf::NodePtr<LineEdit>(widget), column, role});
    refreshMapping(m_mappings.back());
}

void DataWidgetMapper::clearMappings()
{
    m_mappings.clear();
}

int DataWidgetMapper::mappedColumn(const LineEdit* widget) const
{
    for (const Mapping& mapping : m_mappings) {
        if (mapping.widget == widget) {
            return mapping.column;
        }
    }
    return -1;
}

void DataWidgetMapper::setCurrentIndex(const snf::ModelIndex& index)
{
    if (! index.isValid() || index.model() != m_model) {
        setCurrentRowInternal(-1, true);
        return;
    }

    setCurrentRowInternal(index.row(), true);
}

void DataWidgetMapper::setCurrentRow(int row)
{
    setCurrentRowInternal(isValidRow(row) ? row : -1, true);
}

int DataWidgetMapper::currentRow() const
{
    return m_currentRow;
}

snf::ModelIndex DataWidgetMapper::currentIndex(int column) const
{
    if (! m_model || ! isValidRow(m_currentRow) || ! isValidColumn(column)) {
        return {};
    }
    return m_model->index(m_currentRow, column);
}

bool DataWidgetMapper::hasCurrentRow() const
{
    return isValidRow(m_currentRow);
}

void DataWidgetMapper::refresh()
{
    for (const Mapping& mapping : m_mappings) {
        refreshMapping(mapping);
    }
    refreshed.emit();
}

void DataWidgetMapper::revert()
{
    refresh();
}

bool DataWidgetMapper::submit()
{
    if (! m_model || ! isValidRow(m_currentRow)) {
        return false;
    }

    bool attempted = false;
    bool ok = true;
    for (const Mapping& mapping : m_mappings) {
        if (! mapping.widget || ! isValidColumn(mapping.column)) {
            continue;
        }

        const snf::ModelIndex index = m_model->index(m_currentRow, mapping.column);
        if (! index.isValid()) {
            continue;
        }

        attempted = true;
        const std::string next = mapping.widget->text();
        snf::ModelValue currentValue = m_model->data(index, mapping.role);
        if (std::holds_alternative<std::monostate>(currentValue) && mapping.role != snf::ModelDataRole::Display) {
            currentValue = m_model->data(index, snf::ModelDataRole::Display);
        }
        const std::string current = snf::modelValueToString(currentValue);
        if (next == current) {
            continue;
        }

        if (! m_model->setData(index, snf::ModelValue(next), snf::ModelDataRole::Edit)) {
            ok = false;
        }
    }

    if (attempted && ok) {
        submitted.emit();
        refresh();
    }
    return attempted && ok;
}

void DataWidgetMapper::setMappedWidgetsEnabled(bool enabled)
{
    for (const Mapping& mapping : m_mappings) {
        if (mapping.widget) {
            mapping.widget->setEnabled(enabled);
        }
    }
}

bool DataWidgetMapper::isValidRow(int row) const
{
    return m_model && row >= 0 && row < std::max(0, m_model->rowCount());
}

bool DataWidgetMapper::isValidColumn(int column) const
{
    return m_model && column >= 0 && column < std::max(0, m_model->columnCount());
}

void DataWidgetMapper::refreshMapping(const Mapping& mapping)
{
    if (! mapping.widget) {
        return;
    }

    if (! m_model || ! isValidRow(m_currentRow) || ! isValidColumn(mapping.column)) {
        mapping.widget->clear();
        return;
    }

    const snf::ModelIndex index = m_model->index(m_currentRow, mapping.column);
    snf::ModelValue value = m_model->data(index, mapping.role);
    if (std::holds_alternative<std::monostate>(value) && mapping.role != snf::ModelDataRole::Display) {
        value = m_model->data(index, snf::ModelDataRole::Display);
    }
    mapping.widget->setText(snf::modelValueToString(value));
}

void DataWidgetMapper::setCurrentRowInternal(int row, bool refreshWidgets)
{
    const int previous = m_currentRow;
    m_currentRow = row;
    if (m_currentRow != previous) {
        currentRowChanged.emit(m_currentRow);
    }
    if (refreshWidgets) {
        refresh();
    }
}

void DataWidgetMapper::connectModelSignals()
{
    if (! m_model) {
        return;
    }

    m_modelResetConnection = m_model->modelReset.connect([this]() {
        if (! isValidRow(m_currentRow)) {
            setCurrentRowInternal(-1, true);
        } else {
            refresh();
        }
    });
    m_dataChangedConnection = m_model->dataChanged.connect([this](int row, int column) {
        if (row != m_currentRow) {
            return;
        }
        for (const Mapping& mapping : m_mappings) {
            if (mapping.column == column) {
                refreshMapping(mapping);
            }
        }
    });
    m_rowsInsertedConnection = m_model->rowsInserted.connect([this](int first, int count) {
        handleRowsInserted(first, count);
    });
    m_rowsRemovedConnection = m_model->rowsRemoved.connect([this](int first, int count) {
        handleRowsRemoved(first, count);
    });
    m_columnsInsertedConnection = m_model->columnsInserted.connect([this](int first, int count) {
        handleColumnsInserted(first, count);
    });
    m_columnsRemovedConnection = m_model->columnsRemoved.connect([this](int first, int count) {
        handleColumnsRemoved(first, count);
    });
}

void DataWidgetMapper::disconnectModelSignals()
{
    m_modelResetConnection.disconnect();
    m_dataChangedConnection.disconnect();
    m_rowsInsertedConnection.disconnect();
    m_rowsRemovedConnection.disconnect();
    m_columnsInsertedConnection.disconnect();
    m_columnsRemovedConnection.disconnect();
}

void DataWidgetMapper::handleRowsInserted(int first, int count)
{
    if (first < 0 || count <= 0) {
        refresh();
        return;
    }

    if (m_currentRow >= first) {
        setCurrentRowInternal(m_currentRow + count, true);
    } else {
        refresh();
    }
}

void DataWidgetMapper::handleRowsRemoved(int first, int count)
{
    if (first < 0 || count <= 0) {
        refresh();
        return;
    }

    const int last = first + count - 1;
    if (m_currentRow >= first && m_currentRow <= last) {
        setCurrentRowInternal(-1, true);
    } else if (m_currentRow > last) {
        setCurrentRowInternal(m_currentRow - count, true);
    } else {
        refresh();
    }
}

void DataWidgetMapper::handleColumnsInserted(int first, int count)
{
    if (first < 0 || count <= 0) {
        refresh();
        return;
    }

    for (Mapping& mapping : m_mappings) {
        if (mapping.column >= first) {
            mapping.column += count;
        }
    }
    refresh();
}

void DataWidgetMapper::handleColumnsRemoved(int first, int count)
{
    if (first < 0 || count <= 0) {
        refresh();
        return;
    }

    const int last = first + count - 1;
    for (Mapping& mapping : m_mappings) {
        if (mapping.column >= first && mapping.column <= last) {
            mapping.column = -1;
        } else if (mapping.column > last) {
            mapping.column -= count;
        }
    }
    refresh();
}

}  // namespace widgets
}  // namespace snf
