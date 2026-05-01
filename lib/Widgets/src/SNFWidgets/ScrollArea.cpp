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
    const Widget* content = widget();
    if (! content) {
        return {};
    }
    return content->sizeHint();
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

void ScrollArea::renderContent(float viewportWidth, float viewportHeight)
{
    Widget* content = widget();
    if (! content) {
        return;
    }

    const Size hint = content->sizeHint();
    const float width = m_widgetResizable
        ? std::max(viewportWidth, hint.width)
        : (hint.width > 0.0f ? hint.width : viewportWidth);
    const float height = m_widgetResizable
        ? std::max(viewportHeight, hint.height)
        : (hint.height > 0.0f ? hint.height : -1.0f);

    content->renderWidgetConstrained(width, height);
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

    ImGui::PushID(this);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("viewport", ImVec2(width, height), ImGuiChildFlags_None, scrollWindowFlags())) {
        const ImVec2 viewport = ImGui::GetContentRegionAvail();
        renderContent(std::max(0.0f, viewport.x), std::max(0.0f, viewport.y));
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
