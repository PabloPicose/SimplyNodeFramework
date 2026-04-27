#pragma once

/**
 * @file ImGuiDemoWindow.h
 * @brief The built-in Dear ImGui demo window as a SNFWidgets widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

namespace snf {
namespace widgets {

/**
 * @class ImGuiDemoWindow
 * @ingroup SNFWidgets
 * @brief Shows the built-in Dear ImGui demo window.
 *
 * Useful for exploring available ImGui widgets and for testing the
 * SNFWidgets render pipeline.  Add it as a child of the
 * `WebApplicationNode` (or any container widget):
 *
 * @code
 * snf::widgets::ImGuiDemoWindow demo(&webApp);
 * @endcode
 *
 * The window is shown automatically each frame.  Set `setOpen(false)` to
 * hide it, or call `deleteLater()` to remove it permanently.
 */
class ImGuiDemoWindow : public Widget
{
public:
    explicit ImGuiDemoWindow(snf::Node* parent = nullptr);

    /** @brief Returns `true` while the demo window is visible. */
    bool isOpen() const;

    /** @brief Shows or hides the demo window. */
    void setOpen(bool open);

protected:
    void renderImGui() override;

private:
    bool m_open = true;
};

}  // namespace widgets
}  // namespace snf
