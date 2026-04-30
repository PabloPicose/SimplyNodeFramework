#pragma once

/**
 * @file DataWidgetMapper.h
 * @brief Maps one table-model row into editor widgets.
 * @ingroup SNFWidgets
 */

#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Connection.h>
#include <SNFCore/NodePtr.h>
#include <SNFWidgets/LineEdit.h>

#include <vector>

namespace snf {

namespace widgets {

/**
 * @class DataWidgetMapper
 * @ingroup SNFWidgets
 * @brief Synchronises a selected model row with a set of LineEdit widgets.
 *
 * The mapper does not own the model or widgets. Widgets are tracked through
 * NodePtr, so mappings are ignored automatically once a widget is deleted.
 *
 * A common flow is:
 * - map widgets to model columns with `addMapping()`;
 * - call `setCurrentIndex()` from a TableView current-index signal;
 * - call `setMappedWidgetsEnabled(true)` while editing;
 * - call `submit()` to write edited widget text back to the model.
 */
class DataWidgetMapper
{
public:
    DataWidgetMapper();
    ~DataWidgetMapper();

    void setModel(snf::AbstractTableModel* model);
    snf::AbstractTableModel* model() const;

    void addMapping(LineEdit* widget, int column, snf::ModelDataRole role = snf::ModelDataRole::Edit);
    void clearMappings();
    int mappedColumn(const LineEdit* widget) const;

    void setCurrentIndex(const snf::ModelIndex& index);
    void setCurrentRow(int row);
    int currentRow() const;
    snf::ModelIndex currentIndex(int column) const;

    bool hasCurrentRow() const;
    void refresh();
    void revert();
    bool submit();

    void setMappedWidgetsEnabled(bool enabled);

    Signal<int> currentRowChanged;
    Signal<> refreshed;
    Signal<> submitted;

private:
    struct Mapping {
        snf::NodePtr<LineEdit> widget;
        int column = -1;
        snf::ModelDataRole role = snf::ModelDataRole::Edit;
    };

    bool isValidRow(int row) const;
    bool isValidColumn(int column) const;
    void refreshMapping(const Mapping& mapping);
    void setCurrentRowInternal(int row, bool refreshWidgets);
    void connectModelSignals();
    void disconnectModelSignals();
    void handleRowsInserted(int first, int count);
    void handleRowsRemoved(int first, int count);
    void handleColumnsInserted(int first, int count);
    void handleColumnsRemoved(int first, int count);

    snf::AbstractTableModel* m_model = nullptr;
    int m_currentRow = -1;
    std::vector<Mapping> m_mappings;
    Connection m_modelResetConnection;
    Connection m_dataChangedConnection;
    Connection m_rowsInsertedConnection;
    Connection m_rowsRemovedConnection;
    Connection m_columnsInsertedConnection;
    Connection m_columnsRemovedConnection;
};

}  // namespace widgets
}  // namespace snf
