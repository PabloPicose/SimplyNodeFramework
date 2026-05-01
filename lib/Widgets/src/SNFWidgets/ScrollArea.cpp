#include "SNFWidgets/ScrollArea.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

ScrollArea::ScrollArea(snf::Node* parent)
    : Widget(parent)
{
}

void ScrollArea::setWidget(Widget* widget)
{
    if (! widget || widget == this) {
        m_widget = snf::NodePtr<Widget>(nullptr);
        return;
    }

    if (widget->parent() != this) {
        widget->setParent(this);
    }

    m_widget = snf::NodePtr<Widget>(widget);
}

Widget* ScrollArea::widget() const
{
    return m_widget && ! m_widget.isMarkedToDelete() ? m_widget.get() : nullptr;
}

void ScrollArea::setWidgetResizable(bool resizable)
{
    m_widgetResizable = resizable;
}

bool ScrollArea::widgetResizable() const
{
    return m_widgetResizable;
}

void ScrollArea::setVerticalScrollBarPolicy(ScrollBarPolicy policy)
{
    m_verticalPolicy = policy;
}

ScrollArea::ScrollBarPolicy ScrollArea::verticalScrollBarPolicy() const
{
    return m_verticalPolicy;
}

void ScrollArea::setHorizontalScrollBarPolicy(ScrollBarPolicy policy)
{
    m_horizontalPolicy = policy;
}

ScrollArea::ScrollBarPolicy ScrollArea::horizontalScrollBarPolicy() const
{
    return m_horizontalPolicy;
}

Size ScrollArea::sizeHint() const
{
    return {};
}

int ScrollArea::scrollWindowFlags() const
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;

    if (m_verticalPolicy == ScrollBarPolicy::AlwaysOn) {
        flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
    }

    if (m_horizontalPolicy != ScrollBarPolicy::AlwaysOff) {
        flags |= ImGuiWindowFlags_HorizontalScrollbar;
    }

    if (m_horizontalPolicy == ScrollBarPolicy::AlwaysOn) {
        flags |= ImGuiWindowFlags_AlwaysHorizontalScrollbar;
    }

    if (m_verticalPolicy == ScrollBarPolicy::AlwaysOff && m_horizontalPolicy == ScrollBarPolicy::AlwaysOff) {
        flags |= ImGuiWindowFlags_NoScrollbar;
    }

    return flags;
}

ScrollArea::ContentMetrics ScrollArea::contentMetricsForViewport(float viewportWidth, float viewportHeight) const
{
    const Widget* content = widget();
    viewportWidth = std::max(0.0f, viewportWidth);
    viewportHeight = std::max(0.0f, viewportHeight);

    if (! content) {
        return ContentMetrics{viewportWidth, viewportHeight};
    }

    const Size hint = content->sizeHint();
    const float scrollbarSize = ImGui::GetStyle().ScrollbarSize;
    const bool horizontalScrollPossible = m_horizontalPolicy != ScrollBarPolicy::AlwaysOff;
    const bool verticalScrollPossible = m_verticalPolicy != ScrollBarPolicy::AlwaysOff;
    const bool horizontalScrollForced = m_horizontalPolicy == ScrollBarPolicy::AlwaysOn;
    const bool verticalScrollForced = m_verticalPolicy == ScrollBarPolicy::AlwaysOn;

    auto desiredWidth = [this, &hint](float availableWidth) {
        return m_widgetResizable
            ? std::max(availableWidth, hint.width)
            : (hint.width > 0.0f ? hint.width : availableWidth);
    };
    auto desiredHeight = [this, &hint](float availableHeight) {
        return m_widgetResizable
            ? std::max(availableHeight, hint.height)
            : (hint.height > 0.0f ? hint.height : availableHeight);
    };

    float availableWidth = viewportWidth;
    float availableHeight = viewportHeight;
    float width = desiredWidth(availableWidth);
    float height = desiredHeight(availableHeight);
    bool horizontalScroll = horizontalScrollForced || (horizontalScrollPossible && width > availableWidth);
    bool verticalScroll = verticalScrollForced || (verticalScrollPossible && height > availableHeight);

    if (horizontalScroll) {
        availableHeight = std::max(0.0f, viewportHeight - scrollbarSize);
    }
    if (verticalScroll) {
        availableWidth = std::max(0.0f, viewportWidth - scrollbarSize);
    }

    width = desiredWidth(availableWidth);
    height = desiredHeight(availableHeight);
    horizontalScroll = horizontalScrollForced || (horizontalScrollPossible && width > availableWidth);
    verticalScroll = verticalScrollForced || (verticalScrollPossible && height > availableHeight);

    if (horizontalScroll && availableHeight == viewportHeight) {
        availableHeight = std::max(0.0f, viewportHeight - scrollbarSize);
        height = desiredHeight(availableHeight);
    }
    if (verticalScroll && availableWidth == viewportWidth) {
        availableWidth = std::max(0.0f, viewportWidth - scrollbarSize);
        width = desiredWidth(availableWidth);
    }

    return ContentMetrics{std::max(0.0f, width), std::max(0.0f, height)};
}

void ScrollArea::renderContent(float contentWidth, float contentHeight)
{
    Widget* content = widget();
    if (! content) {
        return;
    }

    contentWidth = std::max(0.0f, contentWidth);
    contentHeight = std::max(0.0f, contentHeight);

    if (m_widgetResizable && contentHeight > 0.0f) {
        contentHeight = std::max(0.0f, contentHeight - ImGui::GetStyle().ItemSpacing.y);
    }

    content->renderWidgetConstrained(contentWidth, contentHeight);

}

void ScrollArea::renderWithAvailableSize(float width, float height)
{
    if (width <= 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }
    if (height <= 0.0f) {
        height = ImGui::GetContentRegionAvail().y;
    }

    width = std::max(0.0f, width);
    height = std::max(0.0f, height);
    const ContentMetrics content = contentMetricsForViewport(width, height);
    const float explicitContentWidth = m_horizontalPolicy == ScrollBarPolicy::AlwaysOff ? 0.0f : content.width;
    const float explicitContentHeight = 0.0f;

    ImGui::PushID(this);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowContentSize(ImVec2(explicitContentWidth, explicitContentHeight));
    if (ImGui::BeginChild("viewport", ImVec2(width, height), ImGuiChildFlags_None, scrollWindowFlags())) {
        renderContent(content.width, content.height);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void ScrollArea::renderImGui()
{
    renderWithAvailableSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
}

void ScrollArea::renderImGuiConstrained(float width, float height)
{
    renderWithAvailableSize(width, height);
}

}  // namespace widgets
}  // namespace snf
