#pragma once

/**
 * @file RadioButton.h
 * @brief Interactive radio-button widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>

namespace snf {
namespace widgets {

/**
 * @class RadioButton
 * @ingroup SNFWidgets
 * @brief A labelled radio button for mutually exclusive boolean selection.
 *
 * This class models a single radio button.  Group logic (ensuring only one
 * button per group is active) is the responsibility of the parent widget or
 * application code — the API is designed to be compatible with a future
 * RadioButtonGroup helper without requiring one now.
 *
 * `stateChanged` is emitted (with the new boolean value) only when the
 * effective state changes due to user interaction. Programmatic setters
 * do not emit signals.
 *
 * @code
 * snf::widgets::RadioButton r1("Option A", &win);
 * snf::widgets::RadioButton r2("Option B", &win);
 *
 * r1.stateChanged.connect([&](bool selected) {
 *     if (selected) r2.setSelected(false);
 * });
 * @endcode
 */
class RadioButton : public Widget
{
public:
    /**
     * @param label   Text shown next to the button.
     * @param parent  Parent node (a Window or another container Widget).
     */
    explicit RadioButton(const std::string& label, snf::Node* parent = nullptr);

    /** @brief Sets the radio button label. */
    void setLabel(const std::string& label);

    /** @brief Returns the current label. */
    std::string label() const;

    /** @brief Sets the selected state (does not emit `stateChanged`). */
    void setSelected(bool selected);

    /** @brief Returns @c true if this radio button is currently selected. */
    bool isSelected() const;

    /**
     * @brief Emitted when the user clicks the radio button and the effective
     *        state changes.
     *
     * The argument is the new selected state.
     */
    Signal<bool> stateChanged;

protected:
    void renderImGui() override;

private:
    std::string m_label;
    bool        m_selected = false;
};

}  // namespace widgets
}  // namespace snf
