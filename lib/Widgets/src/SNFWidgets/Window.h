#pragma once

/**
 * @file Window.h
 * @brief ImGui-backed window widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <string>

namespace snf {
namespace widgets {

/**
 * @class Window
 * @ingroup SNFWidgets
 * @brief A resizable, movable Dear ImGui window.
 *
 * Add any `Widget` as a child to populate the window's contents:
 *
 * @code
 * snf::widgets::Window     win("Settings", &webApp);
 * snf::widgets::PushButton btn("Apply",    &win);
 * @endcode
 *
 * The window title can be changed at any time via `setTitle()`.
 */
class Window : public Widget
{
public:
    /**
     * @param title  Window title shown in the title bar.
     * @param parent Parent node (another Widget or an ApplicationNode).
     */
    explicit Window(const std::string& title, snf::Node* parent = nullptr);

    /** @brief Changes the window title. */
    void setTitle(const std::string& title);

    /** @brief Returns the current window title. */
    std::string title() const;

protected:
    void renderImGui() override;

private:
    std::string m_title;
};

}  // namespace widgets
}  // namespace snf
