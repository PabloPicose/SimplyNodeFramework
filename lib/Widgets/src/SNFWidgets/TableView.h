#pragma once

/**
 * @file TableView.h
 * @brief ImGui-backed table view for AbstractTableModel instances.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Connection.h>

namespace snf {
namespace widgets {

/**
 * @class TableView
 * @ingroup SNFWidgets
 * @brief Read-only table widget backed by an external `AbstractTableModel`.
 *
 * `TableView` does not own or destroy its model. The caller must keep the
 * model alive while it is installed in the view, or call `clearModel()` before
 * destroying the model.
 *
 * A null model, empty model, empty headers and empty cells are valid states.
 * Selection indexes use `-1` to mean "no selection". Programmatic selection
 * changes emit the selection signals when the effective selection changes.
 *
 * @code
 * MyModel model;
 * snf::widgets::TableView table(&win);
 * table.setModel(&model);
 *
 * table.currentCellChanged.connect([](int row, int column) {
 *     std::printf("Current cell: %d, %d\n", row, column);
 * });
 * @endcode
 */
class TableView : public Widget
{
public:
    /** @param parent Parent node (a Window or another container Widget). */
    explicit TableView(snf::Node* parent = nullptr);
    ~TableView() override;

    /**
     * @brief Sets the non-owned model used by the view.
     *
     * Passing @c nullptr clears the model. Emits `modelChanged` only when the
     * effective model pointer changes.
     */
    void setModel(snf::AbstractTableModel* model);

    /** @brief Returns the current non-owned model pointer, or @c nullptr. */
    snf::AbstractTableModel* model() const;

    /** @brief Clears the current model pointer. */
    void clearModel();

    /** @brief Enables or disables horizontal header rendering. */
    void setShowHorizontalHeader(bool enabled);

    /** @brief Returns whether horizontal headers are rendered. */
    bool showHorizontalHeader() const;

    /** @brief Enables or disables table grid/border rendering. */
    void setShowGrid(bool enabled);

    /** @brief Returns whether the table grid is rendered. */
    bool showGrid() const;

    /** @brief Enables or disables row-oriented selection highlighting. */
    void setRowSelectionEnabled(bool enabled);

    /** @brief Returns whether row-oriented selection highlighting is enabled. */
    bool rowSelectionEnabled() const;

    /**
     * @brief Sets the current row.
     *
     * Invalid rows clear the row and cell selection. Emits selection signals
     * only when the effective selection changes.
     */
    void setCurrentRow(int row);

    /** @brief Returns the current row, or @c -1 if none is selected. */
    int currentRow() const;

    /**
     * @brief Sets the current cell.
     *
     * Invalid cells clear the selection. Emits selection signals only when
     * the effective selection changes.
     */
    void setCurrentCell(int row, int column);

    /** @brief Returns the current column, or @c -1 if no cell is selected. */
    int currentColumn() const;

    /** @brief Emitted when the current cell changes. Arguments are row, column. */
    Signal<int, int> currentCellChanged;

    /** @brief Emitted when the current row changes. Argument is row. */
    Signal<int> currentRowChanged;

    /** @brief Emitted when the user clicks a cell. Arguments are row, column. */
    Signal<int, int> cellClicked;

    /** @brief Emitted when the user double-clicks a cell. Arguments are row, column. */
    Signal<int, int> cellDoubleClicked;

    /** @brief Emitted when the model pointer changes. Argument is the new model. */
    Signal<snf::AbstractTableModel*> modelChanged;

protected:
    void renderImGui() override;

private:
    bool isValidRow(int row) const;
    bool isValidCell(int row, int column) const;
    void setSelection(int row, int column);
    void validateSelection();
    void connectModelSignals();
    void disconnectModelSignals();

    snf::AbstractTableModel* m_model = nullptr;
    bool                     m_showHorizontalHeader = true;
    bool                     m_showGrid = true;
    bool                     m_rowSelectionEnabled = true;
    int                      m_currentRow = -1;
    int                      m_currentColumn = -1;
    Connection               m_modelResetConnection;
    Connection               m_dataChangedConnection;
};

}  // namespace widgets
}  // namespace snf
