#include "SNFWidgets/TextEdit.h"

#include "imgui.h"

namespace snf {
namespace widgets {

TextEdit::TextEdit(snf::Node* parent)
    : Widget(parent)
{
    syncBuffer();
}

TextEdit::TextEdit(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
    syncBuffer();
}

void TextEdit::setText(const std::string& text)
{
    m_text = text;
    syncBuffer();
}

std::string TextEdit::text() const
{
    return m_text;
}

void TextEdit::clear()
{
    m_text.clear();
    syncBuffer();
}

void TextEdit::setLabel(const std::string& label)
{
    m_label = label;
}

std::string TextEdit::label() const
{
    return m_label;
}

void TextEdit::syncBuffer()
{
    const std::size_t needed = m_text.size() + k_initialCapacity;
    if (m_buffer.size() < needed) {
        m_buffer.resize(needed, '\0');
    }
    std::copy(m_text.begin(), m_text.end(), m_buffer.begin());
    m_buffer[m_text.size()] = '\0';
}

void TextEdit::renderImGui()
{
    // Grow the buffer when within 128 bytes of capacity.
    if (m_text.size() + 128 >= m_buffer.size()) {
        m_buffer.resize(m_buffer.size() + k_initialCapacity, '\0');
    }

    ImGui::PushID(this);

    if (! m_label.empty()) {
        ImGui::TextUnformatted(m_label.c_str());
    }

    ImGui::InputTextMultiline(
        "##input",
        m_buffer.data(),
        m_buffer.size(),
        ImVec2(-FLT_MIN, 0.0f));

    ImGui::PopID();

    const std::string newText(m_buffer.data());
    if (newText != m_text) {
        m_text = newText;
        textChanged.emit(m_text);
    }
}

}  // namespace widgets
}  // namespace snf
