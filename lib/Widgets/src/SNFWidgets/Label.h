#pragma once

/**
 * @file Label.h
 * @brief Non-interactive text label widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <string>

namespace snf {
namespace widgets {

/**
 * @class Label
 * @ingroup SNFWidgets
 * @brief A non-interactive widget that displays a text string.
 *
 * The text is rendered safely: it is never interpreted as a format string.
 * Label has no signals because it carries no user-driven state changes.
 *
 * @code
 * snf::widgets::Window win("Info", &app);
 * snf::widgets::Label  lbl("Hello, world!", &win);
 * lbl.setText("Updated text");
 * @endcode
 */
class Label : public Widget
{
public:
    /**
     * @param text   Initial text to display.
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit Label(const std::string& text = std::string(), snf::Node* parent = nullptr);

    /** @brief Sets the displayed text. */
    void setText(const std::string& text);

    /** @brief Returns the current displayed text. */
    std::string text() const;

protected:
    void renderImGui() override;

private:
    std::string m_text;
};

}  // namespace widgets
}  // namespace snf
