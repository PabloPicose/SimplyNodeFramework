#pragma once

/**
 * @file RadioButton.h
 * @brief Interactive radio-button widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>
#include <SNFCore/NodePtr.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class RadioButton
 * @ingroup SNFWidgets
 * @brief A labelled radio button for mutually exclusive boolean selection.
 *
 * Radio buttons can be linked directly to form an exclusive group without a
 * separate group object. Linking is bidirectional and groups are merged, so
 * linking A to B and B to C makes A, B, and C mutually exclusive.
 *
 * `stateChanged` is emitted (with the new boolean value) only when the
 * effective state changes due to user interaction. Programmatic setters
 * keep the exclusive invariant but do not emit signals.
 *
 * @code
 * snf::widgets::RadioButton r1("Option A", &win);
 * snf::widgets::RadioButton r2("Option B", &win);
 * snf::widgets::RadioButton r3("Option C", &win);
 *
 * r1.link(&r2);
 * r2.link(&r3);
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
     * @brief Links this radio button with another one as part of the same
     *        exclusive group.
     *
     * Passing nullptr or this button is ignored. The link is bidirectional and
     * existing groups are merged, so the caller does not need to link every
     * pair manually.
     */
    void link(RadioButton* other);

    /**
     * @brief Links all non-null buttons in @p buttons into one exclusive group.
     */
    static void linkAll(std::initializer_list<RadioButton*> buttons);

    /** @brief Returns true when @p other belongs to this button's group. */
    bool isLinkedTo(const RadioButton* other) const;

    /** @brief Returns the number of live linked peers. */
    int linkedButtonCount() const;

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
    void setSelectedInternal(bool selected, bool emitSignals);
    void addLinkedPeer(RadioButton* other);
    std::vector<RadioButton*> linkedPeers() const;

    std::string m_label;
    bool        m_selected = false;
    std::vector<snf::NodePtr<RadioButton>> m_linkedButtons;
};

}  // namespace widgets
}  // namespace snf
