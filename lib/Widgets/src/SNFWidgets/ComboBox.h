#pragma once

/**
 * @file ComboBox.h
 * @brief Drop-down combo-box widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class ComboBox
 * @ingroup SNFWidgets
 * @brief A labelled combo box for selecting one item from a string list.
 *
 * Index `-1` represents "no selection" and is the initial state for an empty
 * list or after `clearItems()`.  Setting an index outside the valid range is
 * silently ignored (the current index is not changed).
 *
 * `currentIndexChanged` and `currentTextChanged` are emitted only when the
 * effective selection changes due to user interaction.  Both signals carry the
 * new value; they are emitted together whenever the index (and thus the text)
 * changes.  Programmatic setters do not emit signals, and duplicate emissions
 * within a frame are suppressed.
 *
 * @code
 * snf::widgets::ComboBox combo("Mode", &win);
 * combo.addItems({"Fast", "Medium", "Slow"});
 * combo.setCurrentIndex(0);
 *
 * combo.currentIndexChanged.connect([](int i) {
 *     std::printf("Selected index: %d\n", i);
 * });
 * combo.currentTextChanged.connect([](const std::string& t) {
 *     std::printf("Selected text: %s\n", t.c_str());
 * });
 * @endcode
 */
class ComboBox : public Widget
{
public:
    /**
     * @param label  Text shown next to the combo box.
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit ComboBox(const std::string& label = std::string(), snf::Node* parent = nullptr);

    /** @brief Sets the combo box label. */
    void setLabel(const std::string& label);

    /** @brief Returns the current label. */
    std::string label() const;

    /** @brief Appends a single item to the list. */
    void addItem(const std::string& item);

    /** @brief Appends multiple items to the list. */
    void addItems(const std::vector<std::string>& items);

    /**
     * @brief Removes all items and resets the current index to @c -1.
     *
     * Does not emit signals.
     */
    void clearItems();

    /** @brief Returns the number of items in the list. */
    int count() const;

    /**
     * @brief Sets the current index (does not emit signals).
     *
     * Silently ignored if @p index is out of the range
     * `[-1, count()-1]`.  Use `-1` to clear the selection.
     */
    void setCurrentIndex(int index);

    /** @brief Returns the current index, or @c -1 if no item is selected. */
    int currentIndex() const;

    /**
     * @brief Returns the text of the currently selected item, or an empty
     *        string if no item is selected.
     */
    std::string currentText() const;

    /**
     * @brief Emitted when the current index changes due to user interaction.
     *
     * The argument is the new index (may be @c -1 after `clearItems()`).
     */
    Signal<int> currentIndexChanged;

    /**
     * @brief Emitted when the current text changes due to user interaction.
     *
     * The argument is the new text (empty string when index is @c -1).
     */
    Signal<std::string> currentTextChanged;

protected:
    void renderImGui() override;

private:
    std::string              m_label;
    std::vector<std::string> m_items;
    int                      m_currentIndex = -1;
};

}  // namespace widgets
}  // namespace snf
