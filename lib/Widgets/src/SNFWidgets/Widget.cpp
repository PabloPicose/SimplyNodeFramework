#include "SNFWidgets/Widget.h"

#include "imgui.h"

namespace snf {
namespace widgets {

Widget::Widget(snf::Node* parent) : snf::Node(parent) {}

void Widget::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void Widget::setDisabled(bool disabled)
{
    setEnabled(! disabled);
}

bool Widget::isEnabled() const
{
    return m_enabled;
}

bool Widget::isEffectivelyEnabled() const
{
    if (! m_enabled) {
        return false;
    }

    const snf::Node* current = parent();
    while (current) {
        const auto* widgetParent = dynamic_cast<const Widget*>(current);
        if (widgetParent && ! widgetParent->isEnabled()) {
            return false;
        }
        current = current->parent();
    }

    return true;
}

void Widget::setVisible(bool visible)
{
    m_visible = visible;
}

void Widget::setHidden(bool hidden)
{
    setVisible(! hidden);
}

bool Widget::isVisible() const
{
    return m_visible;
}

bool Widget::isEffectivelyVisible() const
{
    if (! m_visible) {
        return false;
    }

    const snf::Node* current = parent();
    while (current) {
        const auto* widgetParent = dynamic_cast<const Widget*>(current);
        if (widgetParent && ! widgetParent->isVisible()) {
            return false;
        }
        current = current->parent();
    }

    return true;
}

Size Widget::sizeHint() const
{
    return {};
}

bool Widget::containsWidget(const Widget* /*widget*/) const
{
    return false;
}

void Widget::renderWidget()
{
    if (! isEffectivelyVisible()) {
        return;
    }

    const bool disabled = ! isEffectivelyEnabled();
    if (disabled) {
        ImGui::BeginDisabled();
    }

    renderImGui();

    if (disabled) {
        ImGui::EndDisabled();
    }
}

void Widget::renderWidgetConstrained(float width, float height)
{
    if (! isEffectivelyVisible()) {
        return;
    }

    const bool disabled = ! isEffectivelyEnabled();
    if (disabled) {
        ImGui::BeginDisabled();
    }

    renderImGuiConstrained(width, height);

    if (disabled) {
        ImGui::EndDisabled();
    }
}

void Widget::renderImGuiConstrained(float width, float height)
{
    (void)height;

    const bool hasWidth = width > 0.0f;
    if (hasWidth) {
        ImGui::PushItemWidth(width);
    }

    renderImGui();

    if (hasWidth) {
        ImGui::PopItemWidth();
    }
}

void Widget::renderChildren()
{
    for (std::size_t i = 0; i < childrenCount(); ++i) {
        if (auto* child = dynamic_cast<Widget*>(getChild(i))) {
            child->renderWidget();
        }
    }
}

}  // namespace widgets
}  // namespace snf
