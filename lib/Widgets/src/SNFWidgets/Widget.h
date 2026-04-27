#pragma once

/**
 * @file Widget.h
 * @brief Abstract base class for all SNFWidgets UI elements.
 * @ingroup SNFWidgets
 */

#include <SNFCore/Node.h>

namespace snf {
namespace widgets {

/**
 * @class Widget
 * @ingroup SNFWidgets
 * @brief Abstract base for every UI element managed by SNFWidgets.
 *
 * Widgets form a tree through the SNFCore Node parent-child relationship.
 * Attach a widget to a `WebApplicationNode` (or another `Widget`) by passing
 * it as the `parent` argument:
 *
 * @code
 * snf::widgets::Window     win("My Window", &webApp);
 * snf::widgets::PushButton btn("OK",         &win);
 * @endcode
 *
 * The framework calls `renderImGui()` on every top-level widget each frame.
 * Container widgets (e.g. `Window`) forward the call recursively to their
 * Widget children.
 *
 * @note Application code never calls `renderImGui()` directly; it is an
 *       internal framework method.  Dear ImGui headers are NOT part of the
 *       public SNFWidgets API.
 */
class Widget : public snf::Node
{
public:
    explicit Widget(snf::Node* parent = nullptr);

    /** @brief Per-iteration SNFCore update hook (no-op for widgets). */
    void update() override {}

protected:
    /**
     * @brief Called by the framework inside an active Dear ImGui frame.
     *
     * Subclasses implement the ImGui draw calls for their visual appearance.
     * Container subclasses should also call `renderChildren()` to forward
     * rendering to their Widget children.
     */
    virtual void renderImGui() = 0;

    /**
     * @brief Iterates direct children and calls `renderImGui()` on each
     *        `Widget` child.
     *
     * Container widgets (e.g. `Window`) call this from within their own
     * `renderImGui()` to render their contents.
     */
    void renderChildren();

private:
    friend class WebApplicationNode;
    friend class Window;
};

}  // namespace widgets
}  // namespace snf
