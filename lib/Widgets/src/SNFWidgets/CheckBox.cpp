#include "SNFWidgets/CheckBox.h"

#include "imgui.h"

namespace snf {
namespace widgets {

namespace {
std::string visibleLabel(const std::string& label)
{
    const std::size_t marker = label.find("##");
    return marker != std::string::npos ? label.substr(0, marker) : label;
}
}  // namespace
CheckBox::CheckBox(snf::Node* parent)
    : Widget(parent)
{
}

CheckBox::CheckBox(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
}

void CheckBox::setLabel(const std::string& label)
{
    m_label = label;
}

std::string CheckBox::label() const
{
    return m_label;
}

void CheckBox::setTextPlacement(CheckBox::TextPlacement placement)
{
    m_textPlacement = placement;
}

CheckBox::TextPlacement CheckBox::textPlacement() const
{
    return m_textPlacement;
}

void CheckBox::setChecked(bool checked)
{
    m_checked = checked;
}

bool CheckBox::isChecked() const
{
    return m_checked;
}

Size CheckBox::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const float frameHeight = ImGui::GetFrameHeight();
    if (m_textPlacement == TextPlacement::Hidden) {
        return Size{frameHeight, frameHeight};
    }
    const std::string labelText = visibleLabel(m_label);
    const ImVec2 textSize = ImGui::CalcTextSize(labelText.c_str(), nullptr, true);
    const float width = frameHeight
        + (textSize.x > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x + textSize.x : 0.0f);
    return Size{width, frameHeight};
}

void CheckBox::renderImGui()
{
    const bool prev = m_checked;
    ImGui::PushID(this);
    const std::string displayLabel =
        (m_textPlacement == TextPlacement::Hidden) ? std::string{} : visibleLabel(m_label);
    if (ImGui::Checkbox(displayLabel.c_str(), &m_checked)) {
        if (m_checked != prev) {
            stateChanged.emit(m_checked);
        }
    }
    ImGui::PopID();
}

}  // namespace widgets
}  // namespace snf
