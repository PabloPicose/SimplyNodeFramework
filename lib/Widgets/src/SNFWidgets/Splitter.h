#pragma once

/**
 * @file Splitter.h
 * @brief Resizable two-pane splitter container.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <SNFCore/NodePtr.h>

namespace snf {
namespace widgets {

/**
 * @class Splitter
 * @ingroup SNFWidgets
 * @brief Container that divides its area into two resizable panes.
 *
 * A horizontal splitter places the primary pane on the left and the secondary
 * pane on the right. A vertical splitter places the primary pane above the
 * secondary pane. The handle between both panes can be dragged by the user and
 * the resulting ratio is preserved across frames and window resizes.
 */
class Splitter : public Widget
{
public:
    /** @brief Splitting axis. */
    enum class Orientation {
        Horizontal, ///< Primary pane on the left, secondary on the right.
        Vertical,   ///< Primary pane on top, secondary at the bottom.
    };

    /**
     * @brief Constructs a Splitter with the given orientation.
     * @param orientation Initial splitting axis (default: Horizontal).
     * @param parent      Parent node (a Window or another container Widget).
     */
    explicit Splitter(Orientation orientation = Orientation::Horizontal, snf::Node* parent = nullptr);

    /** @brief Sets the splitting axis. */
    void setOrientation(Orientation orientation);
    /** @brief Returns the current splitting axis. */
    Orientation orientation() const;

    /**
     * @brief Sets the widget displayed in the primary pane.
     *
     * The primary pane is the left pane for a horizontal splitter and the
     * top pane for a vertical splitter. Passing @c nullptr clears the pane.
     * The widget is reparented to this splitter if its parent differs.
     */
    void setPrimaryWidget(Widget* widget);

    /**
     * @brief Sets the widget displayed in the secondary pane.
     *
     * The secondary pane is the right pane for a horizontal splitter and the
     * bottom pane for a vertical splitter. Passing @c nullptr clears the pane.
     * The widget is reparented to this splitter if its parent differs.
     */
    void setSecondaryWidget(Widget* widget);

    /**
     * @brief Sets a pane widget by zero-based index.
     * @param index 0 for the primary pane, 1 for the secondary pane.
     *              Out-of-range values are ignored.
     * @param widget Widget to place in the pane, or @c nullptr to clear it.
     */
    void setWidget(int index, Widget* widget);

    /** @brief Returns the current primary pane widget, or @c nullptr. */
    Widget* primaryWidget() const;
    /** @brief Returns the current secondary pane widget, or @c nullptr. */
    Widget* secondaryWidget() const;
    /**
     * @brief Returns the pane widget at zero-based @p index, or @c nullptr.
     * @param index 0 for the primary pane, 1 for the secondary pane.
     */
    Widget* widget(int index) const;

    /**
     * @brief Sets the split ratio immediately.
     * @param ratio Fraction of total pane space given to the primary pane,
     *              clamped to [0, 1].
     */
    void setRatio(float ratio);

    /**
     * @brief Convenience alias for `setRatio()`.
     *
     * Intended to be called once before the first render to communicate the
     * desired starting ratio. Currently equivalent to `setRatio()`.
     */
    void setInitialRatio(float ratio);

    /** @brief Returns the current split ratio (primary fraction, [0, 1]). */
    float ratio() const;

    /**
     * @brief Sets absolute pane sizes in pixels, deriving the ratio from them.
     * @param primarySize   Desired size of the primary pane in pixels (≥ 0).
     * @param secondarySize Desired size of the secondary pane in pixels (≥ 0).
     *
     * The ratio is computed as `primarySize / (primarySize + secondarySize)`.
     * Zero total is ignored and leaves the ratio unchanged.
     */
    void setSizes(float primarySize, float secondarySize);

    /**
     * @brief Convenience alias for `setSizes()`.
     *
     * Intended to be called once before the first render to communicate the
     * desired starting sizes. Currently equivalent to `setSizes()`.
     */
    void setInitialSizes(float primarySize, float secondarySize);

    /** @brief Returns the last rendered primary pane size in pixels. */
    float primarySize() const;
    /** @brief Returns the last rendered secondary pane size in pixels. */
    float secondarySize() const;

    /**
     * @brief Sets both minimum pane sizes simultaneously.
     * @param primaryMinimum   Minimum size for the primary pane in pixels.
     * @param secondaryMinimum Minimum size for the secondary pane in pixels.
     *
     * The user cannot drag the handle past these limits. If the sum of
     * minimums exceeds the available space they are scaled proportionally.
     */
    void setMinimumSizes(float primaryMinimum, float secondaryMinimum);

    /** @brief Sets the minimum size for the primary pane in pixels. */
    void setPrimaryMinimumSize(float size);
    /** @brief Sets the minimum size for the secondary pane in pixels. */
    void setSecondaryMinimumSize(float size);
    /** @brief Returns the minimum size of the primary pane in pixels. */
    float primaryMinimumSize() const;
    /** @brief Returns the minimum size of the secondary pane in pixels. */
    float secondaryMinimumSize() const;

    /**
     * @brief Sets the width (or height) of the drag handle in pixels.
     * @param size Handle thickness in pixels. Values below 1 are clamped to 1.
     */
    void setHandleSize(float size);
    /** @brief Returns the current handle thickness in pixels. */
    float handleSize() const;

    Size sizeHint() const override;

    /**
     * @brief Returns @c true if @p widget is the primary or secondary pane,
     *        or is recursively contained within either pane.
     *
     * Used by `Window` to avoid rendering widgets that are already rendered
     * through the splitter's own pane rendering.
     */
    bool containsWidget(const Widget* widget) const override;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    struct Geometry
    {
        float primarySize = 0.0f;
        float secondarySize = 0.0f;
        float handleOffset = 0.0f;
        float handleSize = 0.0f;
        float totalPaneSize = 0.0f;
    };

    void setPaneWidget(snf::NodePtr<Widget>& pane, Widget* widget);
    void renderWithAvailableSize(float width, float height);
    void renderPane(const char* id, Widget* widget, float width, float height);
    Geometry calculateGeometry(float width, float height) const;
    float clampPrimarySize(float desiredPrimarySize, float totalPaneSize) const;
    void updateRatioFromPrimarySize(float primarySize, float totalPaneSize);
    float effectiveHandleSize() const;

    Orientation m_orientation = Orientation::Horizontal;
    snf::NodePtr<Widget> m_primary{nullptr};
    snf::NodePtr<Widget> m_secondary{nullptr};
    float m_ratio = 0.5f;
    float m_lastPrimarySize = 0.0f;
    float m_lastSecondarySize = 0.0f;
    float m_primaryMinimumSize = 0.0f;
    float m_secondaryMinimumSize = 0.0f;
    float m_handleSize = 8.0f;
};

}  // namespace widgets
}  // namespace snf
