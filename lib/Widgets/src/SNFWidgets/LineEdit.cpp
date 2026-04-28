#include "SNFWidgets/LineEdit.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

namespace {
std::string visibleLabel(const std::string& label)
{
    const std::size_t marker = label.find("##");
    if (marker == std::string::npos) {
        return label;
    }
    return label.substr(0, marker);
}
}  // namespace

LineEdit::LineEdit(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
    syncBuffer();
}

void LineEdit::setText(const std::string& text)
{
    m_text = text;
    syncBuffer();
}

std::string LineEdit::text() const
{
    return m_text;
}

void LineEdit::clear()
{
    m_text.clear();
    syncBuffer();
}

void LineEdit::setLabel(const std::string& label)
{
    m_label = label;
}

std::string LineEdit::label() const
{
    return m_label;
}

void LineEdit::syncBuffer()
{
    // Ensure capacity = text size + max(spare, k_initialCapacity).
    const std::size_t needed = m_text.size() + k_initialCapacity;
    if (m_buffer.size() < needed) {
        m_buffer.resize(needed, '\0');
    }
    // Copy current text into the buffer and null-terminate.
    std::copy(m_text.begin(), m_text.end(), m_buffer.begin());
    m_buffer[m_text.size()] = '\0';
}

void LineEdit::renderImGui()
{
    // Grow the buffer when within 64 bytes of capacity to avoid stalls.
    if (m_text.size() + 64 >= m_buffer.size()) {
        m_buffer.resize(m_buffer.size() + k_initialCapacity, '\0');
    }

    const bool returned = ImGui::InputText(
        m_label.c_str(),
        m_buffer.data(),
        m_buffer.size());

    const std::string newText(m_buffer.data());
    if (newText != m_text) {
        m_text = newText;
        textChanged.emit(m_text);
    }

    if (returned) {
        editingFinished.emit(m_text);
    }
}

void LineEdit::renderImGuiConstrained(float width, float height)
{
    (void)height;

    float inputWidth = width;
    if (width > 0.0f) {
        const std::string labelText = visibleLabel(m_label);
        if (! labelText.empty()) {
            inputWidth -= ImGui::CalcTextSize(labelText.c_str()).x;
            inputWidth -= ImGui::GetStyle().ItemInnerSpacing.x;
        }
        inputWidth = std::max(1.0f, inputWidth);
        ImGui::PushItemWidth(inputWidth);
    }

    renderImGui();

    if (width > 0.0f) {
        ImGui::PopItemWidth();
    }
}

}  // namespace widgets
}  // namespace snf
