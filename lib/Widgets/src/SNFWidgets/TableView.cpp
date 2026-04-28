#include "SNFWidgets/TableView.h"

#include "imgui.h"

#include <algorithm>
#include <string>

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
    m_rowSelectionEnabled = enabled;
}

bool TableView::rowSelectionEnabled() const
{
    return m_rowSelectionEnabled;
}

void TableView::setCurrentRow(int row)
{
    if (! isValidRow(row)) {
        setSelection(-1, -1);
        return;
    }

    int column = m_currentColumn;
    if (! isValidCell(row, column)) {
        column = -1;
    }
    setSelection(row, column);
}

int TableView::currentRow() const
{
    return m_currentRow;
}

void TableView::setCurrentCell(int row, int column)
{
    if (! isValidCell(row, column)) {
        setSelection(-1, -1);
        return;
    }

    setSelection(row, column);
}

int TableView::currentColumn() const
{
    return m_currentColumn;
}

bool TableView::isValidRow(int row) const
{
    return m_model && row >= 0 && row < safeCount(m_model->rowCount());
}

bool TableView::isValidCell(int row, int column) const
{
    return isValidRow(row) && column >= 0 && column < safeCount(m_model->columnCount());
}

void TableView::setSelection(int row, int column)
{
    const int previousRow = m_currentRow;
    const int previousColumn = m_currentColumn;

    m_currentRow = row;
    m_currentColumn = column;

    if (m_currentRow != previousRow) {
        currentRowChanged.emit(m_currentRow);
    }

    if (m_currentRow != previousRow || m_currentColumn != previousColumn) {
        currentCellChanged.emit(m_currentRow, m_currentColumn);
    }
}

void TableView::validateSelection()
{
    if (! m_model) {
        setSelection(-1, -1);
        return;
    }

    if (! isValidRow(m_currentRow)) {
        setSelection(-1, -1);
        return;
    }

    if (m_currentColumn != -1 && ! isValidCell(m_currentRow, m_currentColumn)) {
        setSelection(m_currentRow, -1);
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

                const std::string cell = m_model->data(row, column);
                const bool selected = m_rowSelectionEnabled
                    ? (row == m_currentRow)
                    : (row == m_currentRow && column == m_currentColumn);

                ImGui::PushID(row);
                ImGui::PushID(column);
                const char* label = cell.empty() ? " " : cell.c_str();
                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    setCurrentCell(row, column);
                    cellClicked.emit(row, column);
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    setCurrentCell(row, column);
                    cellDoubleClicked.emit(row, column);
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
