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
 * Attach a widget to an `ApplicationNode` (or another `Widget`) by passing
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

    /**
     * @brief Enables or disables user interaction with this widget.
     *
     * A disabled widget is still rendered, but user input is ignored and the
     * framework applies the platform's disabled visual treatment. Programmatic
     * setters keep working; this only affects interaction through the UI.
     *
     * Child widgets inherit the disabled state of their widget ancestors when
     * rendered through SNFWidgets containers/layouts.
     */
    void setEnabled(bool enabled);

    /** @brief Convenience inverse of `setEnabled()`. */
    void setDisabled(bool disabled);

    /** @brief Returns this widget's local enabled state. */
    bool isEnabled() const;

    /**
     * @brief Returns whether this widget and all widget ancestors are enabled.
     */
    bool isEffectivelyEnabled() const;

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
     * @brief Renders the widget with optional layout constraints.
     *
     * Layout containers call this instead of `renderImGui()` when they want
     * the next ImGui item to consume a specific width or height.  The default
     * implementation applies an ImGui item width when @p width is positive and
     * then delegates to `renderImGui()`. Widgets that support an explicit
     * height (for example buttons) may override it.
     */
    virtual void renderImGuiConstrained(float width, float height);

    /**
     * @brief Iterates direct children and calls `renderImGui()` on each
     *        `Widget` child.
     *
     * Container widgets (e.g. `Window`) call this from within their own
     * `renderImGui()` to render their contents.
     */
    void renderChildren();

    /** @brief Framework entry point that applies common widget state. */
    void renderWidget();

    /** @brief Framework entry point with layout constraints and common state. */
    void renderWidgetConstrained(float width, float height);

private:
    friend class ApplicationNode;
    friend class Layout;
    friend class Window;

    bool m_enabled = true;
};

}  // namespace widgets
}  // namespace snf
