#pragma once

/**
 * @file PushButton.h
 * @brief Clickable push-button widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>

namespace snf {
namespace widgets {

/**
 * @class PushButton
 * @ingroup SNFWidgets
 * @brief A labelled push button that emits `clicked` when pressed.
 *
 * @code
 * snf::widgets::Window     win("Tools", &webApp);
 * snf::widgets::PushButton btn("Run",   &win);
 *
 * btn.clicked.connect([]() {
 *     std::printf("Run clicked\n");
 * });
 * @endcode
 */
class PushButton : public Widget
{
public:
    /**
     * @param label  Text shown inside the button.
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit PushButton(const std::string& label, snf::Node* parent = nullptr);

    /** @brief Changes the button label. */
    void setLabel(const std::string& label);

    /** @brief Returns the current button label. */
    std::string label() const;

    /** @brief Emitted each frame that the button is pressed. */
    Signal<> clicked;

protected:
    void renderImGui() override;

private:
    std::string m_label;
};

}  // namespace widgets
}  // namespace snf
