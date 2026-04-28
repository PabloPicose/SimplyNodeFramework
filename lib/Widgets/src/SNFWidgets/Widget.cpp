#include "SNFWidgets/Widget.h"

#include "imgui.h"

namespace snf {
namespace widgets {

Widget::Widget(snf::Node* parent) : snf::Node(parent) {}

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
            child->renderImGui();
        }
    }
}

}  // namespace widgets
}  // namespace snf
