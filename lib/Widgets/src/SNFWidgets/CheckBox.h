#pragma once

/**
 * @file CheckBox.h
 * @brief Interactive boolean checkbox widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>

namespace snf {
namespace widgets {

/**
 * @class CheckBox
 * @ingroup SNFWidgets
 * @brief A labelled checkbox that can be checked or unchecked.
 *
 * `stateChanged` is emitted (with the new boolean value) only when the
 * effective state changes due to user interaction. Programmatic setters
 * do not emit signals, and no duplicate emission occurs within a frame
 * when the value is unchanged.
 *
 * @code
 * snf::widgets::Window   win("Settings", &app);
 * snf::widgets::CheckBox chk("Enable feature", &win);
 *
 * chk.stateChanged.connect([](bool checked) {
 *     std::printf("Checked: %s\n", checked ? "yes" : "no");
 * });
 * @endcode
 */
class CheckBox : public Widget
{
public:
    /**
     * @param label   Text shown next to the checkbox.
     * @param parent  Parent node (a Window or another container Widget).
     */
    explicit CheckBox(const std::string& label, snf::Node* parent = nullptr);

    /** @brief Sets the checkbox label. */
    void setLabel(const std::string& label);

    /** @brief Returns the current label. */
    std::string label() const;

    /** @brief Sets the checked state (does not emit `stateChanged`). */
    void setChecked(bool checked);

    /** @brief Returns @c true if the checkbox is currently checked. */
    bool isChecked() const;

    Size sizeHint() const override;

    /**
     * @brief Emitted when the user toggles the checkbox.
     *
     * The argument is the new checked state. Not emitted when the effective
     * value has not changed.
     */
    Signal<bool> stateChanged;

protected:
    void renderImGui() override;

private:
    std::string m_label;
    bool        m_checked = false;
};

}  // namespace widgets
}  // namespace snf
