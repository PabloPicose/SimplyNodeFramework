#include "SNFWidgets/TableView.h"

#include "imgui.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <variant>

#include <SNFCore/AbstractTableModel.h>

namespace snf {
namespace widgets {

namespace {
int safeCount(int count)
{
    return std::max(0, count);
}

constexpr float k_minimumColumnWidth = 120.0f;

float tableBorderWidth(bool showGrid, int columns)
{
    if (! showGrid || columns <= 0) {
        return 0.0f;
    }
    return static_cast<float>(columns + 1);
}

float normalizedColorComponent(float component)
{
    return std::clamp(component, 0.0f, 1.0f);
}

bool colorFromModelValue(const snf::Variant& value, ImU32& color)
{
    if (! value.holds<snf::ModelColor>()) {
        return false;
    }

    const snf::ModelColor& modelColor = value.toColor();
    color = ImGui::GetColorU32(ImVec4(normalizedColorComponent(modelColor.red),
                                      normalizedColorComponent(modelColor.green),
                                      normalizedColorComponent(modelColor.blue),
                                      normalizedColorComponent(modelColor.alpha)));
    return true;
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

void TableView::setStretchLastColumn(bool enabled)
{
    m_stretchLastColumn = enabled;
}

bool TableView::stretchLastColumn() const
{
    return m_stretchLastColumn;
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

void TableView::handleRowsInserted(int first, int count)
{
    if (first < 0 || count <= 0) {
        validateSelection();
        return;
    }

    const std::vector<SelectionKey> previousSelection = m_selection;
    const bool hasColumnSelection = std::any_of(m_selection.begin(), m_selection.end(), [](const SelectionKey& key) {
        return key.row < 0 && key.column >= 0;
    });
    for (SelectionKey& key : m_selection) {
        if (key.row >= first) {
            key.row += count;
        }
    }

    if (m_currentRow >= first) {
        updateCurrent(m_currentRow + count, m_currentColumn);
    }

    if (m_selection != previousSelection || hasColumnSelection) {
        selectionChanged.emit();
    }
    validateSelection();
}

void TableView::handleRowsRemoved(int first, int count)
{
    if (first < 0 || count <= 0) {
        validateSelection();
        return;
    }

    const int last = first + count - 1;
    const std::vector<SelectionKey> previousSelection = m_selection;
    const bool hasColumnSelection = std::any_of(m_selection.begin(), m_selection.end(), [](const SelectionKey& key) {
        return key.row < 0 && key.column >= 0;
    });
    m_selection.erase(std::remove_if(m_selection.begin(),
                                     m_selection.end(),
                                     [first, last](const SelectionKey& key) {
                                         return key.row >= first && key.row <= last;
                                     }),
                      m_selection.end());

    for (SelectionKey& key : m_selection) {
        if (key.row > last) {
            key.row -= count;
        }
    }

    if (m_currentRow >= first && m_currentRow <= last) {
        updateCurrent(-1, -1);
    } else if (m_currentRow > last) {
        updateCurrent(m_currentRow - count, m_currentColumn);
    }

    if (m_selection != previousSelection || hasColumnSelection) {
        selectionChanged.emit();
    }
    validateSelection();
}

void TableView::handleColumnsInserted(int first, int count)
{
    if (first < 0 || count <= 0) {
        validateSelection();
        return;
    }

    const std::vector<SelectionKey> previousSelection = m_selection;
    const bool hasRowSelection = std::any_of(m_selection.begin(), m_selection.end(), [](const SelectionKey& key) {
        return key.row >= 0 && key.column < 0;
    });
    for (SelectionKey& key : m_selection) {
        if (key.column >= first) {
            key.column += count;
        }
    }

    if (m_currentColumn >= first) {
        updateCurrent(m_currentRow, m_currentColumn + count);
    }

    if (m_selection != previousSelection || hasRowSelection) {
        selectionChanged.emit();
    }
    validateSelection();
}

void TableView::handleColumnsRemoved(int first, int count)
{
    if (first < 0 || count <= 0) {
        validateSelection();
        return;
    }

    const int last = first + count - 1;
    const std::vector<SelectionKey> previousSelection = m_selection;
    const bool hasRowSelection = std::any_of(m_selection.begin(), m_selection.end(), [](const SelectionKey& key) {
        return key.row >= 0 && key.column < 0;
    });
    m_selection.erase(std::remove_if(m_selection.begin(),
                                     m_selection.end(),
                                     [first, last](const SelectionKey& key) {
                                         return key.column >= first && key.column <= last;
                                     }),
                      m_selection.end());

    for (SelectionKey& key : m_selection) {
        if (key.column > last) {
            key.column -= count;
        }
    }

    if (m_currentColumn >= first && m_currentColumn <= last) {
        updateCurrent(-1, -1);
    } else if (m_currentColumn > last) {
        updateCurrent(m_currentRow, m_currentColumn - count);
    }

    if (m_selection != previousSelection || hasRowSelection) {
        selectionChanged.emit();
    }
    validateSelection();
}

void TableView::connectModelSignals()
{
    if (! m_model) {
        return;
    }

    m_modelResetConnection = m_model->modelReset.connect([this]() { validateSelection(); });
    m_dataChangedConnection = m_model->dataChanged.connect([this](int, int) { validateSelection(); });
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

void TableView::disconnectModelSignals()
{
    m_modelResetConnection.disconnect();
    m_dataChangedConnection.disconnect();
    m_rowsInsertedConnection.disconnect();
    m_rowsRemovedConnection.disconnect();
    m_columnsInsertedConnection.disconnect();
    m_columnsRemovedConnection.disconnect();
}

Size TableView::sizeHint() const
{
    if (! m_model || ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const int rows = safeCount(m_model->rowCount());
    const int columns = safeCount(m_model->columnCount());
    if (columns == 0) {
        return {};
    }

    float width = 0.0f;
    for (const float columnWidth : naturalColumnWidths()) {
        width += columnWidth;
    }
    width += tableBorderWidth(m_showGrid, columns);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowHeight = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
    const float headerHeight = m_showHorizontalHeader ? rowHeight : 0.0f;
    const int rowBands = rows + (m_showHorizontalHeader ? 1 : 0);
    const float height = headerHeight
        + rowHeight * static_cast<float>(rows)
        + (m_showGrid ? static_cast<float>(rowBands + 1) : 0.0f);
    return Size{width, height};
}

std::vector<float> TableView::naturalColumnWidths() const
{
    std::vector<float> widths;
    if (! m_model || ImGui::GetCurrentContext() == nullptr) {
        return widths;
    }

    const int rows = safeCount(m_model->rowCount());
    const int columns = safeCount(m_model->columnCount());
    const ImGuiStyle& style = ImGui::GetStyle();
    widths.reserve(static_cast<std::size_t>(columns));

    for (int column = 0; column < columns; ++column) {
        float columnWidth = ImGui::CalcTextSize(m_model->headerData(column).c_str()).x;
        for (int row = 0; row < rows; ++row) {
            const snf::ModelIndex index = m_model->index(row, column);
            const std::string cell = m_model->data(index).toString();
            columnWidth = std::max(columnWidth, ImGui::CalcTextSize(cell.c_str()).x);
        }
        widths.push_back(std::max(k_minimumColumnWidth, columnWidth + style.CellPadding.x * 2.0f));
    }

    return widths;
}

void TableView::renderTable(float width, float height)
{
    if (! m_model) {
        return;
    }

    const int rows = safeCount(m_model->rowCount());
    const int columns = safeCount(m_model->columnCount());
    if (columns == 0) {
        return;
    }

    const bool constrainedWidth = width > 0.0f;
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
    flags |= constrainedWidth ? ImGuiTableFlags_SizingFixedFit : ImGuiTableFlags_SizingStretchProp;
    if (m_showGrid) {
        flags |= ImGuiTableFlags_Borders;
    }

    const std::vector<float> columnWidths = naturalColumnWidths();
    float naturalWidth = tableBorderWidth(m_showGrid, columns);
    for (const float columnWidth : columnWidths) {
        naturalWidth += columnWidth;
    }

    const ImVec2 outerSize(
        constrainedWidth ? width : 0.0f,
        height > 0.0f ? height : 0.0f);

    ImGui::PushID(this);
    if (ImGui::BeginTable("TableView", columns, flags, outerSize)) {
        const bool stretchLastColumn = m_stretchLastColumn && constrainedWidth && width > naturalWidth;
        for (int column = 0; column < columns; ++column) {
            const std::string header = m_model->headerData(column);
            if (constrainedWidth) {
                if (stretchLastColumn && column == columns - 1) {
                    ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                } else {
                    const float columnWidth = columnWidths[static_cast<std::size_t>(column)];
                    ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, columnWidth);
                }
            } else {
                ImGui::TableSetupColumn(header.c_str());
            }
        }

        if (m_showHorizontalHeader) {
            ImGui::TableHeadersRow();
        }

        for (int row = 0; row < rows; ++row) {
            ImGui::TableNextRow();
            for (int column = 0; column < columns; ++column) {
                ImGui::TableSetColumnIndex(column);

                const snf::ModelIndex index = m_model->index(row, column);
                const std::string cell = m_model->data(index).toString();
                const bool selected = containsSelectionKey(keyForCell(row, column));
                ImU32 decorationColor = 0;
                if (colorFromModelValue(m_model->data(index, snf::ModelDataRole::Decoration), decorationColor)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, decorationColor);
                }

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

void TableView::renderImGui()
{
    renderTable(-1.0f, -1.0f);
}

void TableView::renderImGuiConstrained(float width, float height)
{
    renderTable(width, height);
}

}  // namespace widgets
}  // namespace snf
