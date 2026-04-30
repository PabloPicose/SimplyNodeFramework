#include "SNFWidgets/TableView.h"

#include "imgui.h"

#include <algorithm>
#include <iterator>
#include <string>

#include <SNFCore/AbstractTableModel.h>

namespace snf {
namespace widgets {

namespace {
int safeCount(int count)
{
    return std::max(0, count);
}
}  // namespace

TableView::TableView(snf::Node* parent)
    : Widget(parent)
{
}

TableView::~TableView()
{
    disconnectModelSignals();
}

void TableView::setModel(snf::AbstractTableModel* model)
{
    if (m_model == model) {
        return;
    }

    disconnectModelSignals();
    m_model = model;
    connectModelSignals();
    validateSelection();
    modelChanged.emit(m_model);
}

snf::AbstractTableModel* TableView::model() const
{
    return m_model;
}

void TableView::clearModel()
{
    setModel(nullptr);
}

void TableView::setShowHorizontalHeader(bool enabled)
{
    m_showHorizontalHeader = enabled;
}

bool TableView::showHorizontalHeader() const
{
    return m_showHorizontalHeader;
}

void TableView::setShowGrid(bool enabled)
{
    m_showGrid = enabled;
}

bool TableView::showGrid() const
{
    return m_showGrid;
}

void TableView::setRowSelectionEnabled(bool enabled)
{
    setSelectionBehavior(enabled ? TableSelectionBehavior::Rows : TableSelectionBehavior::Cells);
}

bool TableView::rowSelectionEnabled() const
{
    return m_selectionBehavior == TableSelectionBehavior::Rows;
}

void TableView::setSelectionBehavior(TableSelectionBehavior behavior)
{
    if (m_selectionBehavior == behavior) {
        return;
    }

    m_selectionBehavior = behavior;
    clearSelection();
}

TableSelectionBehavior TableView::selectionBehavior() const
{
    return m_selectionBehavior;
}

void TableView::setSelectionMode(TableSelectionMode mode)
{
    if (m_selectionMode == mode) {
        return;
    }

    m_selectionMode = mode;
    if (m_selectionMode == TableSelectionMode::None) {
        clearSelection();
    } else if (m_selectionMode == TableSelectionMode::Single && m_selection.size() > 1) {
        m_selection.erase(std::next(m_selection.begin()), m_selection.end());
        selectionChanged.emit();
    }
}

TableSelectionMode TableView::selectionMode() const
{
    return m_selectionMode;
}

void TableView::setCurrentRow(int row)
{
    if (! isValidRow(row)) {
        clearSelection();
        return;
    }

    int column = m_currentColumn;
    if (! isValidCell(row, column)) {
        column = -1;
    }
    updateCurrent(row, column);

    if (m_selectionMode == TableSelectionMode::None) {
        return;
    }

    applySelection(SelectionKey{row, -1}, TableSelectionMode::Multiple == m_selectionMode ? SelectionOperation::Add
                                                                                           : SelectionOperation::Replace);
}

int TableView::currentRow() const
{
    return m_currentRow;
}

void TableView::setCurrentCell(int row, int column)
{
    if (! isValidCell(row, column)) {
        clearSelection();
        return;
    }

    selectCell(row, column);
}

int TableView::currentColumn() const
{
    return m_currentColumn;
}

snf::ModelIndex TableView::currentIndex() const
{
    return m_model ? m_model->index(m_currentRow, m_currentColumn) : snf::ModelIndex();
}

void TableView::selectRow(int row)
{
    if (! isValidRow(row)) {
        clearSelection();
        return;
    }

    updateCurrent(row, -1);
    if (m_selectionMode != TableSelectionMode::None) {
        applySelection(SelectionKey{row, -1}, m_selectionMode == TableSelectionMode::Multiple ? SelectionOperation::Add
                                                                                              : SelectionOperation::Replace);
    }
}

void TableView::selectColumn(int column)
{
    if (! isValidColumn(column)) {
        clearSelection();
        return;
    }

    const int row = isValidRow(m_currentRow) ? m_currentRow : 0;
    updateCurrent(row, column);
    if (m_selectionMode != TableSelectionMode::None) {
        applySelection(SelectionKey{-1, column}, m_selectionMode == TableSelectionMode::Multiple ? SelectionOperation::Add
                                                                                                 : SelectionOperation::Replace);
    }
}

void TableView::selectCell(int row, int column)
{
    if (! isValidCell(row, column)) {
        clearSelection();
        return;
    }

    updateCurrent(row, column);
    if (m_selectionMode != TableSelectionMode::None) {
        applySelection(keyForCell(row, column),
                       m_selectionMode == TableSelectionMode::Multiple ? SelectionOperation::Add
                                                                       : SelectionOperation::Replace);
    }
}

void TableView::clearSelection()
{
    const bool hadSelection = ! m_selection.empty();
    m_selection.clear();
    updateCurrent(-1, -1);
    if (hadSelection) {
        selectionChanged.emit();
    }
}

std::vector<int> TableView::selectedRows() const
{
    std::vector<int> rows;
    if (! m_model) {
        return rows;
    }

    auto addUnique = [&rows](int row) {
        if (std::find(rows.begin(), rows.end(), row) == rows.end()) {
            rows.push_back(row);
        }
    };

    for (const SelectionKey& key : m_selection) {
        if (key.row >= 0) {
            addUnique(key.row);
        } else if (key.column >= 0) {
            for (int row = 0; row < safeCount(m_model->rowCount()); ++row) {
                addUnique(row);
            }
        }
    }
    return rows;
}

std::vector<int> TableView::selectedColumns() const
{
    std::vector<int> columns;
    if (! m_model) {
        return columns;
    }

    auto addUnique = [&columns](int column) {
        if (std::find(columns.begin(), columns.end(), column) == columns.end()) {
            columns.push_back(column);
        }
    };

    for (const SelectionKey& key : m_selection) {
        if (key.column >= 0) {
            addUnique(key.column);
        } else if (key.row >= 0) {
            for (int column = 0; column < safeCount(m_model->columnCount()); ++column) {
                addUnique(column);
            }
        }
    }
    return columns;
}

std::vector<snf::ModelIndex> TableView::selectedIndexes() const
{
    std::vector<snf::ModelIndex> indexes;
    if (! m_model) {
        return indexes;
    }

    auto addIndex = [this, &indexes](int row, int column) {
        const snf::ModelIndex index = m_model->index(row, column);
        if (index.isValid() && std::find(indexes.begin(), indexes.end(), index) == indexes.end()) {
            indexes.push_back(index);
        }
    };

    for (const SelectionKey& key : m_selection) {
        if (key.row >= 0 && key.column >= 0) {
            addIndex(key.row, key.column);
        } else if (key.row >= 0) {
            for (int column = 0; column < safeCount(m_model->columnCount()); ++column) {
                addIndex(key.row, column);
            }
        } else if (key.column >= 0) {
            for (int row = 0; row < safeCount(m_model->rowCount()); ++row) {
                addIndex(row, key.column);
            }
        }
    }
    return indexes;
}

bool TableView::isValidRow(int row) const
{
    return m_model && row >= 0 && row < safeCount(m_model->rowCount());
}

bool TableView::isValidColumn(int column) const
{
    return m_model && column >= 0 && column < safeCount(m_model->columnCount());
}

bool TableView::isValidCell(int row, int column) const
{
    return isValidRow(row) && isValidColumn(column);
}

bool TableView::isValidSelectionKey(const SelectionKey& key) const
{
    if (key.row >= 0 && key.column >= 0) {
        return isValidCell(key.row, key.column);
    }
    if (key.row >= 0) {
        return isValidRow(key.row);
    }
    if (key.column >= 0) {
        return isValidColumn(key.column);
    }
    return false;
}

bool TableView::containsSelectionKey(const SelectionKey& key) const
{
    return std::find(m_selection.begin(), m_selection.end(), key) != m_selection.end();
}

TableView::SelectionKey TableView::keyForCell(int row, int column) const
{
    switch (m_selectionBehavior) {
    case TableSelectionBehavior::Rows:
        return SelectionKey{row, -1};
    case TableSelectionBehavior::Columns:
        return SelectionKey{-1, column};
    case TableSelectionBehavior::Cells:
        return SelectionKey{row, column};
    }
    return SelectionKey{row, column};
}

void TableView::updateCurrent(int row, int column)
{
    const int previousRow = m_currentRow;
    const int previousColumn = m_currentColumn;
    const snf::ModelIndex previousIndex = currentIndex();

    m_currentRow = row;
    m_currentColumn = column;
    const snf::ModelIndex nextIndex = currentIndex();

    if (m_currentRow != previousRow) {
        currentRowChanged.emit(m_currentRow);
    }

    if (m_currentColumn != previousColumn) {
        currentColumnChanged.emit(m_currentColumn);
    }

    if (m_currentRow != previousRow || m_currentColumn != previousColumn) {
        currentCellChanged.emit(m_currentRow, m_currentColumn);
    }

    if (previousIndex != nextIndex) {
        currentIndexChanged.emit(nextIndex);
    }
}

void TableView::applySelection(const SelectionKey& key, SelectionOperation operation)
{
    if (m_selectionMode == TableSelectionMode::None || ! isValidSelectionKey(key)) {
        return;
    }

    const std::vector<SelectionKey> previous = m_selection;
    auto existing = std::find(m_selection.begin(), m_selection.end(), key);

    if (operation == SelectionOperation::Replace || m_selectionMode == TableSelectionMode::Single) {
        m_selection.clear();
        m_selection.push_back(key);
    } else if (operation == SelectionOperation::Add) {
        if (existing == m_selection.end()) {
            m_selection.push_back(key);
        }
    } else if (operation == SelectionOperation::Toggle) {
        if (existing == m_selection.end()) {
            m_selection.push_back(key);
        } else {
            m_selection.erase(existing);
        }
    }

    if (m_selection != previous) {
        selectionChanged.emit();
    }
}

void TableView::validateSelection()
{
    if (! m_model) {
        clearSelection();
        return;
    }

    const std::vector<SelectionKey> previousSelection = m_selection;
    m_selection.erase(std::remove_if(m_selection.begin(),
                                     m_selection.end(),
                                     [this](const SelectionKey& key) { return ! isValidSelectionKey(key); }),
                      m_selection.end());

    if (m_selection != previousSelection) {
        selectionChanged.emit();
    }

    if (! isValidRow(m_currentRow)) {
        updateCurrent(-1, -1);
        return;
    }

    if (m_currentColumn != -1 && ! isValidCell(m_currentRow, m_currentColumn)) {
        updateCurrent(m_currentRow, -1);
    }
}

void TableView::connectModelSignals()
{
    if (! m_model) {
        return;
    }

    m_modelResetConnection = m_model->modelReset.connect([this]() { validateSelection(); });
    m_dataChangedConnection = m_model->dataChanged.connect([this](int, int) { validateSelection(); });
}

void TableView::disconnectModelSignals()
{
    m_modelResetConnection.disconnect();
    m_dataChangedConnection.disconnect();
}

void TableView::renderImGui()
{
    if (! m_model) {
        return;
    }

    const int rows = safeCount(m_model->rowCount());
    const int columns = safeCount(m_model->columnCount());
    if (columns == 0) {
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (m_showGrid) {
        flags |= ImGuiTableFlags_Borders;
    }

    ImGui::PushID(this);
    if (ImGui::BeginTable("TableView", columns, flags)) {
        for (int column = 0; column < columns; ++column) {
            const std::string header = m_model->headerData(column);
            ImGui::TableSetupColumn(header.c_str());
        }

        if (m_showHorizontalHeader) {
            ImGui::TableHeadersRow();
        }

        for (int row = 0; row < rows; ++row) {
            ImGui::TableNextRow();
            for (int column = 0; column < columns; ++column) {
                ImGui::TableSetColumnIndex(column);

                const snf::ModelIndex index = m_model->index(row, column);
                const std::string cell = snf::modelValueToString(m_model->data(index));
                const bool selected = containsSelectionKey(keyForCell(row, column));

                ImGui::PushID(row);
                ImGui::PushID(column);
                const char* label = cell.empty() ? " " : cell.c_str();
                const ImGuiSelectableFlags selectableFlags = m_selectionBehavior == TableSelectionBehavior::Rows
                    ? ImGuiSelectableFlags_SpanAllColumns
                    : ImGuiSelectableFlags_None;
                if (ImGui::Selectable(label, selected, selectableFlags)) {
                    updateCurrent(row, column);
                    const bool toggle = m_selectionMode == TableSelectionMode::Multiple && ImGui::GetIO().KeyCtrl;
                    applySelection(keyForCell(row, column), toggle ? SelectionOperation::Toggle
                                                                   : SelectionOperation::Replace);
                    cellClicked.emit(row, column);
                    indexClicked.emit(index);
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    updateCurrent(row, column);
                    cellDoubleClicked.emit(row, column);
                    indexDoubleClicked.emit(index);
                }
                ImGui::PopID();
                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopID();
}

}  // namespace widgets
}  // namespace snf
