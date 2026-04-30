#pragma once

/**
 * @file AbstractTableModel.h
 * @brief Abstract data model for table-oriented views.
 * @ingroup SNFCore
 */

#include <SNFCore/Connection.h>
#include <SNFCore/ModelIndex.h>

#include <string>

namespace snf {

/**
 * @class AbstractTableModel
 * @ingroup SNFCore
 * @brief Base class for tabular data consumed by table views.
 *
 * `AbstractTableModel` separates table data from rendering. Views query row
 * and column counts, cell strings and header strings every frame; concrete
 * models own the actual data.
 *
 * The first version intentionally uses `std::string` for cells and headers.
 * Editing is opt-in: reimplement `isEditable()` and `setData()` in a derived
 * model if needed. The default implementation is read-only.
 *
 * @code
 * class MyModel : public snf::AbstractTableModel {
 * public:
 *     int rowCount() const override { return 2; }
 *     int columnCount() const override { return 2; }
 *     std::string data(int row, int column) const override {
 *         return (row == 0 && column == 0) ? "Name" : "";
 *     }
 *     std::string headerData(int section) const override {
 *         return section == 0 ? "Column A" : "Column B";
 *     }
 * };
 * @endcode
 */
class AbstractTableModel
{
public:
    virtual ~AbstractTableModel();

    /** @brief Returns the number of rows in the model. */
    virtual int rowCount() const = 0;

    /** @brief Returns the number of columns in the model. */
    virtual int columnCount() const = 0;

    /** @brief Returns a valid index for @p row and @p column, or an invalid index. */
    ModelIndex index(int row, int column) const;

    /**
     * @brief Returns the string value for a cell.
     *
     * Implementations should return an empty string for invalid indexes.
     */
    virtual std::string data(int row, int column) const = 0;

    /** @brief Returns the string value for @p index. */
    virtual std::string data(const ModelIndex& index) const;

    /**
     * @brief Returns the horizontal header string for @p section.
     *
     * The default implementation returns an empty string.
     */
    virtual std::string headerData(int section) const;

    /**
     * @brief Returns the vertical header string for @p section.
     *
     * The default implementation returns an empty string.
     */
    virtual std::string verticalHeaderData(int section) const;

    /**
     * @brief Returns whether a cell can be edited.
     *
     * The default implementation returns @c false.
     */
    virtual bool isEditable(int row, int column) const;

    /** @brief Returns whether @p index can be edited. */
    virtual bool isEditable(const ModelIndex& index) const;

    /**
     * @brief Updates a cell value if the model supports editing.
     *
     * The default implementation leaves the model unchanged and returns
     * @c false. Implementations that change data should emit `dataChanged`.
     */
    virtual bool setData(int row, int column, const std::string& value);

    /** @brief Updates @p index if the model supports editing. */
    virtual bool setData(const ModelIndex& index, const std::string& value);

    /** @brief Emitted when a cell value changes. Arguments are row, column. */
    Signal<int, int> dataChanged;

    /** @brief Emitted when the model content or dimensions are reset. */
    Signal<> modelReset;

    /** @brief Reserved for future row insertion support. Arguments are first, count. */
    Signal<int, int> rowsInserted;

    /** @brief Reserved for future row removal support. Arguments are first, count. */
    Signal<int, int> rowsRemoved;

    /** @brief Reserved for future column insertion support. Arguments are first, count. */
    Signal<int, int> columnsInserted;

    /** @brief Reserved for future column removal support. Arguments are first, count. */
    Signal<int, int> columnsRemoved;

protected:
    /** @brief Emits `dataChanged`. */
    void notifyDataChanged(int row, int column);

    /** @brief Emits `modelReset`. */
    void notifyModelReset();

    /** @brief Emits `rowsInserted`. */
    void notifyRowsInserted(int first, int count);

    /** @brief Emits `rowsRemoved`. */
    void notifyRowsRemoved(int first, int count);

    /** @brief Emits `columnsInserted`. */
    void notifyColumnsInserted(int first, int count);

    /** @brief Emits `columnsRemoved`. */
    void notifyColumnsRemoved(int first, int count);
};

}  // namespace snf
