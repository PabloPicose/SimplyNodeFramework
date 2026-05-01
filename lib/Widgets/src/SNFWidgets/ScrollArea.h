#pragma once

/**
 * @file ScrollArea.h
 * @brief Scrollable container widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <SNFCore/NodePtr.h>

namespace snf {
namespace widgets {

/**
 * @class ScrollArea
 * @ingroup SNFWidgets
 * @brief Container that renders one child widget inside a scrollable viewport.
 */
class ScrollArea : public Widget
{
public:
    enum class ScrollBarPolicy {
        AsNeeded,
        AlwaysOff,
        AlwaysOn,
    };

    explicit ScrollArea(snf::Node* parent = nullptr);

    void setWidget(Widget* widget);
    Widget* widget() const;

    void setWidgetResizable(bool resizable);
    bool widgetResizable() const;

    void setVerticalScrollBarPolicy(ScrollBarPolicy policy);
    ScrollBarPolicy verticalScrollBarPolicy() const;

    void setHorizontalScrollBarPolicy(ScrollBarPolicy policy);
    ScrollBarPolicy horizontalScrollBarPolicy() const;

    Size sizeHint() const override;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    struct ContentMetrics
    {
        float width = 0.0f;
        float height = 0.0f;
    };

    void renderWithAvailableSize(float width, float height);
    void renderContent(float contentWidth, float contentHeight);
    int scrollWindowFlags() const;
    ContentMetrics contentMetricsForViewport(float viewportWidth, float viewportHeight) const;

    snf::NodePtr<Widget> m_widget{nullptr};
    ScrollBarPolicy m_verticalPolicy = ScrollBarPolicy::AsNeeded;
    ScrollBarPolicy m_horizontalPolicy = ScrollBarPolicy::AsNeeded;
    bool m_widgetResizable = false;
};

}  // namespace widgets
}  // namespace snf
