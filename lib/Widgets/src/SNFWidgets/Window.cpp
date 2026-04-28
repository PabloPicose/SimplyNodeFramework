#include "SNFWidgets/Window.h"

#include "SNFWidgets/Layout.h"

#include "imgui.h"  // internal to SNFWidgets, not part of the public API

namespace snf {
namespace widgets {

Window::Window(const std::string& title, snf::Node* parent)
    : Widget(parent), m_title(title)
{
}

void Window::setTitle(const std::string& title)
{
    m_title = title;
}

std::string Window::title() const
{
    return m_title;
}

void Window::setLayout(Layout* layout)
{
    m_layout = layout;
    if (m_layout && m_layout->parent() != this) {
        m_layout->setParent(this);
    }
}

Layout* Window::layout() const
{
    return m_layout;
}

void Window::renderImGui()
{
    // ImGui::Begin() / ImGui::End() must always be paired, even when the
    // window is collapsed or clipped.  Children are rendered only when the
    // window body is visible (Begin returns true).
    if (ImGui::Begin(m_title.c_str())) {
        if (! m_layout) {
            renderChildren();
        } else {
            for (std::size_t i = 0; i < childrenCount(); ++i) {
                auto* child = dynamic_cast<Widget*>(getChild(i));
                if (! child) {
                    continue;
                }

                if (child == m_layout) {
                    m_layout->renderImGui();
                    continue;
                }

                if (! m_layout->containsWidget(child)) {
                    child->renderImGui();
                }
            }
        }
    }
    ImGui::End();
}

}  // namespace widgets
}  // namespace snf
