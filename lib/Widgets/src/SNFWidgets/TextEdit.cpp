#include "SNFWidgets/TextEdit.h"

#include "imgui.h"

#include <algorithm>

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

Size TextEdit::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const float labelHeight = m_label.empty()
        ? 0.0f
        : ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float minWidth = ImGui::GetFrameHeight() * 6.0f;
    return Size{minWidth, labelHeight + ImGui::GetFrameHeightWithSpacing() * 6.0f};
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
    renderInput(-1.0f, 0.0f);
}

void TextEdit::renderImGuiConstrained(float width, float height)
{
    renderInput(width, height);
}

void TextEdit::renderInput(float width, float height)
{
    // Grow the buffer when within 128 bytes of capacity.
    if (m_text.size() + 128 >= m_buffer.size()) {
        m_buffer.resize(m_buffer.size() + k_initialCapacity, '\0');
    }

    ImGui::PushID(this);

    if (! m_label.empty()) {
        const ImVec2 labelPos = ImGui::GetCursorScreenPos();
        ImGui::TextUnformatted(m_label.c_str());
        // TextUnformatted resets the cursor X to the window's left edge.
        // Restore it so InputTextMultiline renders in the correct column.
        const ImVec2 afterLabel = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(labelPos.x, afterLabel.y));
    }

    const float inputWidth = width > 0.0f ? width : -FLT_MIN;
    float inputHeight = height > 0.0f ? height : 0.0f;
    if (inputHeight > 0.0f && ! m_label.empty()) {
        inputHeight = std::max(
            0.0f,
            inputHeight - ImGui::GetTextLineHeight() - ImGui::GetStyle().ItemSpacing.y);
    }

    ImGui::InputTextMultiline(
        "##input",
        m_buffer.data(),
        m_buffer.size(),
        ImVec2(inputWidth, inputHeight));

    ImGui::PopID();

    const std::string newText(m_buffer.data());
    if (newText != m_text) {
        m_text = newText;
        textChanged.emit(m_text);
    }
}

}  // namespace widgets
}  // namespace snf
