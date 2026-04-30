#pragma once

/**
 * @file TableView.h
 * @brief ImGui-backed table view for AbstractTableModel instances.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>
#include <SNFCore/ModelIndex.h>

#include <vector>

namespace snf {

    class AbstractTableModel;
namespace widgets {

/**
 * @enum TableSelectionBehavior
 * @ingroup SNFWidgets
 * @brief Describes which table unit is selected when a user interacts with a cell.
 */
enum class TableSelectionBehavior {
    Rows,    ///< Selecting a cell selects its whole row.
    Columns, ///< Selecting a cell selects its whole column.
    Cells,   ///< Selecting a cell selects only that cell.
};

/**
 * @enum TableSelectionMode
 * @ingroup SNFWidgets
 * @brief Describes how many items can be selected.
 */
enum class TableSelectionMode {
    None,     ///< User and programmatic selection is disabled.
    Single,   ///< A single row, column, or cell can be selected.
    Multiple, ///< Multiple rows, columns, or cells can be selected.
};

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

    /** @brief Sets whether selection applies to rows, columns, or cells. */
    void setSelectionBehavior(TableSelectionBehavior behavior);

    /** @brief Returns the current selection behavior. */
    TableSelectionBehavior selectionBehavior() const;

    /** @brief Sets whether none, one, or multiple items can be selected. */
    void setSelectionMode(TableSelectionMode mode);

    /** @brief Returns the current selection mode. */
    TableSelectionMode selectionMode() const;

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

    /** @brief Returns the current cell index, or an invalid index if none is current. */
    snf::ModelIndex currentIndex() const;

    /** @brief Selects @p row according to the current selection mode. */
    void selectRow(int row);

    /** @brief Selects @p column according to the current selection mode. */
    void selectColumn(int column);

    /** @brief Selects @p row, @p column according to the current selection mode. */
    void selectCell(int row, int column);

    /** @brief Clears current and selected indexes. */
    void clearSelection();

    /** @brief Returns selected row numbers. */
    std::vector<int> selectedRows() const;

    /** @brief Returns selected column numbers. */
    std::vector<int> selectedColumns() const;

    /** @brief Returns selected cell indexes. Row/column selections are expanded to cells. */
    std::vector<snf::ModelIndex> selectedIndexes() const;

    /** @brief Emitted when the current cell changes. Arguments are row, column. */
    Signal<int, int> currentCellChanged;

    /** @brief Emitted when the current row changes. Argument is row. */
    Signal<int> currentRowChanged;

    /** @brief Emitted when the current column changes. Argument is column. */
    Signal<int> currentColumnChanged;

    /** @brief Emitted when the current cell index changes. */
    Signal<snf::ModelIndex> currentIndexChanged;

    /** @brief Emitted when the selected rows, columns, or cells change. */
    Signal<> selectionChanged;

    /** @brief Emitted when the user clicks a cell. Arguments are row, column. */
    Signal<int, int> cellClicked;

    /** @brief Emitted when the user double-clicks a cell. Arguments are row, column. */
    Signal<int, int> cellDoubleClicked;

    /** @brief Emitted when the user clicks a cell. Argument is the cell index. */
    Signal<snf::ModelIndex> indexClicked;

    /** @brief Emitted when the user double-clicks a cell. Argument is the cell index. */
    Signal<snf::ModelIndex> indexDoubleClicked;

    /** @brief Emitted when the model pointer changes. Argument is the new model. */
    Signal<snf::AbstractTableModel*> modelChanged;

protected:
    void renderImGui() override;

private:
    struct SelectionKey {
        int row = -1;
        int column = -1;

        friend bool operator==(const SelectionKey& lhs, const SelectionKey& rhs)
        {
            return lhs.row == rhs.row && lhs.column == rhs.column;
        }
    };

    enum class SelectionOperation {
        Replace,
        Add,
        Toggle,
    };

    bool isValidRow(int row) const;
    bool isValidColumn(int column) const;
    bool isValidCell(int row, int column) const;
    bool isValidSelectionKey(const SelectionKey& key) const;
    bool containsSelectionKey(const SelectionKey& key) const;
    SelectionKey keyForCell(int row, int column) const;
    void updateCurrent(int row, int column);
    void applySelection(const SelectionKey& key, SelectionOperation operation);
    void validateSelection();
    void connectModelSignals();
    void disconnectModelSignals();

    snf::AbstractTableModel* m_model = nullptr;
    bool                     m_showHorizontalHeader = true;
    bool                     m_showGrid = true;
    TableSelectionBehavior   m_selectionBehavior = TableSelectionBehavior::Rows;
    TableSelectionMode       m_selectionMode = TableSelectionMode::Single;
    int                      m_currentRow = -1;
    int                      m_currentColumn = -1;
    std::vector<SelectionKey> m_selection;
    Connection               m_modelResetConnection;
    Connection               m_dataChangedConnection;
};

}  // namespace widgets
}  // namespace snf
