#include "SNFWidgets/PushButton.h"

#include "imgui.h"  // internal to SNFWidgets, not part of the public API

namespace snf {
namespace widgets {

PushButton::PushButton(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
}

void PushButton::setLabel(const std::string& label)
{
    m_label = label;
}

std::string PushButton::label() const
{
    return m_label;
}

Size PushButton::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const ImVec2 textSize = ImGui::CalcTextSize(m_label.c_str(), nullptr, true);
    const ImGuiStyle& style = ImGui::GetStyle();
    return Size{
        textSize.x + style.FramePadding.x * 2.0f,
        textSize.y + style.FramePadding.y * 2.0f};
}

void PushButton::renderImGui()
{
    if (ImGui::Button(m_label.c_str())) {
        clicked.emit();
    }
}

void PushButton::renderImGuiConstrained(float width, float height)
{
    const ImVec2 size(width > 0.0f ? width : 0.0f, height > 0.0f ? height : 0.0f);
    if (ImGui::Button(m_label.c_str(), size)) {
        clicked.emit();
    }
}

}  // namespace widgets
}  // namespace snf
