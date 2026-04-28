#include "SNFWidgets/CheckBox.h"

#include "imgui.h"

namespace snf {
namespace widgets {

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

void CheckBox::setChecked(bool checked)
{
    m_checked = checked;
}

bool CheckBox::isChecked() const
{
    return m_checked;
}

void CheckBox::renderImGui()
{
    const bool prev = m_checked;
    if (ImGui::Checkbox(m_label.c_str(), &m_checked)) {
        if (m_checked != prev) {
            stateChanged.emit(m_checked);
        }
    }
}

}  // namespace widgets
}  // namespace snf
