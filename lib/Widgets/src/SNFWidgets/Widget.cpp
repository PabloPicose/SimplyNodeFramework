#include "SNFWidgets/Widget.h"

namespace snf {
namespace widgets {

Widget::Widget(snf::Node* parent) : snf::Node(parent) {}

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
