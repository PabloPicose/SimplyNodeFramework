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
    enum class Orientation {
        Horizontal,
        Vertical,
    };

    explicit Splitter(Orientation orientation = Orientation::Horizontal, snf::Node* parent = nullptr);

    void setOrientation(Orientation orientation);
    Orientation orientation() const;

    void setPrimaryWidget(Widget* widget);
    void setSecondaryWidget(Widget* widget);
    void setWidget(int index, Widget* widget);

    Widget* primaryWidget() const;
    Widget* secondaryWidget() const;
    Widget* widget(int index) const;

    void setRatio(float ratio);
    void setInitialRatio(float ratio);
    float ratio() const;

    void setSizes(float primarySize, float secondarySize);
    void setInitialSizes(float primarySize, float secondarySize);
    float primarySize() const;
    float secondarySize() const;

    void setMinimumSizes(float primaryMinimum, float secondaryMinimum);
    void setPrimaryMinimumSize(float size);
    void setSecondaryMinimumSize(float size);
    float primaryMinimumSize() const;
    float secondaryMinimumSize() const;

    void setHandleSize(float size);
    float handleSize() const;

    Size sizeHint() const override;
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
