#include "SNFWidgets/ImGuiDemoWindow.h"

#include "imgui.h"  // internal to SNFWidgets, not part of the public API

namespace snf {
namespace widgets {

ImGuiDemoWindow::ImGuiDemoWindow(snf::Node* parent) : Widget(parent) {}

bool ImGuiDemoWindow::isOpen() const
{
    return m_open;
}

void ImGuiDemoWindow::setOpen(bool open)
{
    m_open = open;
}

void ImGuiDemoWindow::renderImGui()
{
    if (m_open) {
        ImGui::ShowDemoWindow(&m_open);
    }
}

}  // namespace widgets
}  // namespace snf
