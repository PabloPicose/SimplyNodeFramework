#include "SNFWidgets/RadioButton.h"

#include "imgui.h"

namespace snf {
namespace widgets {

RadioButton::RadioButton(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
}

void RadioButton::setLabel(const std::string& label)
{
    m_label = label;
}

std::string RadioButton::label() const
{
    return m_label;
}

void RadioButton::setSelected(bool selected)
{
    m_selected = selected;
}

bool RadioButton::isSelected() const
{
    return m_selected;
}

void RadioButton::renderImGui()
{
    // ImGui::RadioButton(label, active) renders a single radio button that
    // returns true when clicked regardless of its previous state. We track
    // state ourselves to detect actual changes and emit only on real change.
    if (ImGui::RadioButton(m_label.c_str(), m_selected)) {
        if (!m_selected) {
            m_selected = true;
            stateChanged.emit(m_selected);
        }
    }
}

}  // namespace widgets
}  // namespace snf
