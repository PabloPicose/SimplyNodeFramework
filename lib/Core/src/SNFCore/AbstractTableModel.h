#pragma once

/**
 * @file AbstractTableModel.h
 * @brief Abstract data model for table-oriented views.
 * @ingroup SNFCore
 */

#include <SNFCore/Connection.h>
#include <SNFCore/ModelIndex.h>

#include <cstdint>
#include <string>
#include <variant>

namespace snf {

/**
 * @enum ModelDataRole
 * @ingroup SNFCore
 * @brief Describes the purpose for data requested from a model index.
 */
enum class ModelDataRole {
    Display,    ///< Human-readable value for views.
    Edit,       ///< Editable value used by editors.
    Decoration, ///< Optional visual decoration value for views.
};

/**
 * @struct ModelColor
 * @ingroup SNFCore
 * @brief RGBA color value used by decoration roles.
 *
 * Components are expected in the `[0, 1]` range. Views may clamp values when
 * converting to their backend color representation.
 */
struct ModelColor {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;

    friend bool operator==(const ModelColor& lhs, const ModelColor& rhs)
    {
        return lhs.red == rhs.red
            && lhs.green == rhs.green
            && lhs.blue == rhs.blue
            && lhs.alpha == rhs.alpha;
    }

    friend bool operator!=(const ModelColor& lhs, const ModelColor& rhs)
    {
        return ! (lhs == rhs);
    }
};

/**
 * @brief Type-safe model value used by table models.
 */
using ModelValue = std::variant<std::monostate, bool, int, std::int64_t, double, std::string, ModelColor>;

/** @brief Converts a ModelValue into display text. */
std::string modelValueToString(const ModelValue& value);

/**
 * @class AbstractTableModel
 * @ingroup SNFCore
 * @brief Base class for tabular data consumed by table views.
 *
 * `AbstractTableModel` separates table data from rendering. Views query row
 * and column counts, cell values and header strings every frame; concrete
 * models own the actual data.
 *
 * Editing is opt-in: reimplement `isEditable()`, `setData()` and the
 * structural insert/remove functions in a derived model if needed. The default
 * implementation is read-only and fixed-size.
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

    /** @brief Returns the value for @p index and @p role. */
    virtual ModelValue data(const ModelIndex& index, ModelDataRole role = ModelDataRole::Display) const;

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
    virtual bool setData(const ModelIndex& index,
                         const ModelValue& value,
                         ModelDataRole role = ModelDataRole::Edit);

    /**
     * @brief Inserts @p count rows before @p row if the model supports it.
     *
     * Valid insertion positions are in the range `[0, rowCount()]`. Models
     * that change their dimensions should emit `rowsInserted`.
     */
    virtual bool insertRows(int row, int count);

    /**
     * @brief Removes @p count rows starting at @p row if the model supports it.
     *
     * Models that change their dimensions should emit `rowsRemoved`.
     */
    virtual bool removeRows(int row, int count);

    /**
     * @brief Inserts @p count columns before @p column if the model supports it.
     *
     * Valid insertion positions are in the range `[0, columnCount()]`. Models
     * that change their dimensions should emit `columnsInserted`.
     */
    virtual bool insertColumns(int column, int count);

    /**
     * @brief Removes @p count columns starting at @p column if the model supports it.
     *
     * Models that change their dimensions should emit `columnsRemoved`.
     */
    virtual bool removeColumns(int column, int count);

    /** @brief Emitted when a cell value changes. Arguments are row, column. */
    Signal<int, int> dataChanged;

    /** @brief Emitted when the model content or dimensions are reset. */
    Signal<> modelReset;

    /** @brief Emitted after rows are inserted. Arguments are first, count. */
    Signal<int, int> rowsInserted;

    /** @brief Emitted after rows are removed. Arguments are first, count. */
    Signal<int, int> rowsRemoved;

    /** @brief Emitted after columns are inserted. Arguments are first, count. */
    Signal<int, int> columnsInserted;

    /** @brief Emitted after columns are removed. Arguments are first, count. */
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
