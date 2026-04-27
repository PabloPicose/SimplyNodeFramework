#include "SNFWidgets/Window.h"

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

void Window::renderImGui()
{
    // ImGui::Begin() / ImGui::End() must always be paired, even when the
    // window is collapsed or clipped.  Children are rendered only when the
    // window body is visible (Begin returns true).
    if (ImGui::Begin(m_title.c_str())) {
        renderChildren();
    }
    ImGui::End();
}

}  // namespace widgets
}  // namespace snf
