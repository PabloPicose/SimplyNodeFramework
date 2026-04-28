#include "SNFWidgets/Label.h"

#include "imgui.h"

namespace snf {
namespace widgets {

Label::Label(const std::string& text, snf::Node* parent)
    : Widget(parent), m_text(text)
{
}

void Label::setText(const std::string& text)
{
    m_text = text;
}

std::string Label::text() const
{
    return m_text;
}

void Label::renderImGui()
{
    // Use %s to avoid interpreting m_text as a format string.
    ImGui::TextUnformatted(m_text.c_str());
}

}  // namespace widgets
}  // namespace snf
