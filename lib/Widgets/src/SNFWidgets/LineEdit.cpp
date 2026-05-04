#include "SNFWidgets/LineEdit.h"

#include "imgui.h"

#include <algorithm>
#include <array>

namespace snf {
namespace widgets {

namespace {
constexpr float k_defaultInputWidth = 128.0f;

std::string visibleLabel(const std::string& label)
{
    const std::size_t marker = label.find("##");
    if (marker == std::string::npos) {
        return label;
    }
    return label.substr(0, marker);
}

float clampedWidth(float width)
{
    return std::max(0.0f, width);
}
}  // namespace

LineEdit::LineEdit(snf::Node* parent)
    : Widget(parent)
{
    syncBuffer();
}

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

void LineEdit::setPlaceholder(const std::string& placeholder)
{
    m_placeholder = placeholder;
}

std::string LineEdit::placeholder() const
{
    return m_placeholder;
}

void LineEdit::setAuxiliaryText(const std::string& text)
{
    m_auxiliaryText = text;
}

std::string LineEdit::auxiliaryText() const
{
    return m_auxiliaryText;
}

void LineEdit::setLayoutPolicy(LayoutPolicy policy)
{
    m_layoutPolicy = policy;
}

LineEdit::LayoutPolicy LineEdit::layoutPolicy() const
{
    return m_layoutPolicy;
}

void LineEdit::setTextPlacement(TextPlacement placement)
{
    m_textPlacement = placement;
}

LineEdit::TextPlacement LineEdit::textPlacement() const
{
    return m_textPlacement;
}

void LineEdit::setTextOverflow(TextOverflow overflow)
{
    m_textOverflow = overflow;
}

LineEdit::TextOverflow LineEdit::textOverflow() const
{
    return m_textOverflow;
}

void LineEdit::setMinimumInputWidth(float width)
{
    m_minimumInputWidth = clampedWidth(width);
}

float LineEdit::minimumInputWidth() const
{
    return m_minimumInputWidth;
}

void LineEdit::setPreferredInputWidth(float width)
{
    m_preferredInputWidth = clampedWidth(width);
}

float LineEdit::preferredInputWidth() const
{
    return m_preferredInputWidth;
}

void LineEdit::setCompanionTextWidth(float width)
{
    m_companionTextWidth = clampedWidth(width);
}

float LineEdit::companionTextWidth() const
{
    return m_companionTextWidth;
}

void LineEdit::setReadOnly(bool readOnly)     { m_readOnly = readOnly; }
bool LineEdit::readOnly() const               { return m_readOnly; }

void LineEdit::setAutoSelectAll(bool enabled) { m_autoSelectAll = enabled; }
bool LineEdit::autoSelectAll() const          { return m_autoSelectAll; }

void LineEdit::setPassword(bool enabled)      { m_password = enabled; }
bool LineEdit::password() const               { return m_password; }

void LineEdit::setEscapeClearsAll(bool enabled) { m_escapeClearsAll = enabled; }
bool LineEdit::escapeClearsAll() const          { return m_escapeClearsAll; }

Size LineEdit::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const std::string labelText = visibleLabel(m_label);
    const bool hasLabel = ! labelText.empty() && m_textPlacement != TextPlacement::Hidden;
    const bool hasAuxiliary = ! m_auxiliaryText.empty();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;

    float width = std::max(m_preferredInputWidth, effectiveMinimumInputWidth());
    if (hasLabel) {
        width += ImGui::CalcTextSize(labelText.c_str()).x + spacing;
    }
    if (hasAuxiliary) {
        width += ImGui::CalcTextSize(m_auxiliaryText.c_str()).x + spacing;
    }

    return Size{width, ImGui::GetFrameHeight()};
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
    renderWithAvailableWidth(ImGui::GetContentRegionAvail().x);
}

bool LineEdit::renderInputText(float width)
{
    // Grow the buffer when within 64 bytes of capacity to avoid stalls.
    if (m_text.size() + 64 >= m_buffer.size()) {
        m_buffer.resize(m_buffer.size() + k_initialCapacity, '\0');
    }

    const bool hasWidth = width > 0.0f;
    if (hasWidth) {
        ImGui::PushItemWidth(width);
    }

    const ImGuiInputTextFlags flags = buildFlags();
    bool returned = false;
    if (m_placeholder.empty()) {
        returned = ImGui::InputText("##input", m_buffer.data(), m_buffer.size(), flags);
    } else {
        returned = ImGui::InputTextWithHint(
            "##input",
            m_placeholder.c_str(),
            m_buffer.data(),
            m_buffer.size(),
            flags);
    }

    if (hasWidth) {
        ImGui::PopItemWidth();
    }

    const std::string newText(m_buffer.data());
    if (newText != m_text) {
        m_text = newText;
        textChanged.emit(m_text);
    }

    if (returned) {
        editingFinished.emit(m_text);
    }

    return returned;
}

float LineEdit::effectiveMinimumInputWidth() const
{
    return m_minimumInputWidth > 0.0f ? m_minimumInputWidth : k_defaultInputWidth;
}

bool LineEdit::renderCompanionText(const std::string& text, float width) const
{
    if (text.empty() || width <= 0.0f) {
        return false;
    }

    const float naturalWidth = ImGui::CalcTextSize(text.c_str()).x;
    if (m_textOverflow == TextOverflow::Hide && naturalWidth > width) {
        return false;
    }

    const float frameHeight = ImGui::GetFrameHeight();
    const float textHeight = ImGui::CalcTextSize(text.c_str()).y;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textPos(pos.x, pos.y + std::max(0.0f, (frameHeight - textHeight) * 0.5f));
    const ImVec4 clip(pos.x, pos.y, pos.x + width, pos.y + frameHeight);
    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(),
        ImGui::GetFontSize(),
        textPos,
        ImGui::GetColorU32(ImGuiCol_Text),
        text.c_str(),
        text.c_str() + text.size(),
        0.0f,
        &clip);
    return true;
}

void LineEdit::renderWithAvailableWidth(float width)
{
    ImGui::PushID(this);

    const std::string labelText = visibleLabel(m_label);
    const bool hasLabel = ! labelText.empty() && m_textPlacement != TextPlacement::Hidden;
    const bool labelLeft = hasLabel && m_textPlacement == TextPlacement::Left;
    const bool labelRight = hasLabel && m_textPlacement == TextPlacement::Right;
    const bool hasAuxiliary = ! m_auxiliaryText.empty();

    struct TextPart
    {
        const std::string* text = nullptr;
        bool beforeInput = false;
        float naturalWidth = 0.0f;
        float allocatedWidth = 0.0f;
    };

    std::array<TextPart, 2> parts{{
        {hasLabel ? &labelText : nullptr, labelLeft, hasLabel ? ImGui::CalcTextSize(labelText.c_str()).x : 0.0f, 0.0f},
        {hasAuxiliary ? &m_auxiliaryText : nullptr, false, hasAuxiliary ? ImGui::CalcTextSize(m_auxiliaryText.c_str()).x : 0.0f, 0.0f},
    }};

    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    int textPartCount = 0;
    float naturalTextWidth = 0.0f;
    for (const auto& part : parts) {
        if (part.text) {
            ++textPartCount;
            naturalTextWidth += part.naturalWidth;
        }
    }

    const float available = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const bool hasAvailableWidth = available > 0.0f;
    const float minimumInput = std::max(1.0f, effectiveMinimumInputWidth());
    const float inputGaps = spacing * static_cast<float>(textPartCount);
    float textBudget = naturalTextWidth;
    float inputWidth = m_preferredInputWidth > 0.0f ? m_preferredInputWidth : 0.0f;

    if (hasAvailableWidth) {
        const float fixedTextWidth = m_companionTextWidth > 0.0f ? m_companionTextWidth : naturalTextWidth;
        switch (m_layoutPolicy) {
        case LayoutPolicy::TextPriority:
            textBudget = std::min(naturalTextWidth, std::max(0.0f, available - minimumInput - inputGaps));
            inputWidth = std::max(minimumInput, available - textBudget - inputGaps);
            break;
        case LayoutPolicy::InputPriority:
            inputWidth = m_preferredInputWidth > 0.0f ? m_preferredInputWidth : available;
            inputWidth = std::min(available, std::max(minimumInput, inputWidth));
            textBudget = std::min(naturalTextWidth, std::max(0.0f, available - inputWidth - inputGaps));
            break;
        case LayoutPolicy::FixedTextWidth:
            textBudget = std::min(fixedTextWidth, std::max(0.0f, available - minimumInput - inputGaps));
            inputWidth = std::max(minimumInput, available - textBudget - inputGaps);
            break;
        case LayoutPolicy::InputExpands:
            if (m_preferredInputWidth > 0.0f) {
                inputWidth = std::min(available, std::max(minimumInput, m_preferredInputWidth));
                textBudget = std::min(naturalTextWidth, std::max(0.0f, available - inputWidth - inputGaps));
            } else {
                textBudget = std::min(naturalTextWidth, std::max(0.0f, available - minimumInput - inputGaps));
                inputWidth = std::max(minimumInput, available - textBudget - inputGaps);
                inputWidth = std::min(inputWidth, available);
                textBudget = std::min(naturalTextWidth, std::max(0.0f, available - inputWidth - inputGaps));
            }
            break;
        }
    } else if (inputWidth <= 0.0f) {
        inputWidth = 0.0f;
    }

    float remainingTextWidth = textBudget;
    for (auto& part : parts) {
        if (! part.text) {
            continue;
        }
        part.allocatedWidth = std::min(part.naturalWidth, std::max(0.0f, remainingTextWidth));
        remainingTextWidth = std::max(0.0f, remainingTextWidth - part.allocatedWidth);
    }

    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float frameHeight = ImGui::GetFrameHeight();
    float x = start.x;
    for (const auto& part : parts) {
        if (part.text && part.beforeInput) {
            renderCompanionText(*part.text, part.allocatedWidth);
            x += part.allocatedWidth + spacing;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(x, start.y));
    renderInputText(inputWidth);
    x += inputWidth;

    if (labelRight) {
        x += spacing;
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        renderCompanionText(labelText, parts[0].allocatedWidth);
        x += parts[0].allocatedWidth;
    }

    if (hasAuxiliary) {
        x += spacing;
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        renderCompanionText(m_auxiliaryText, parts[1].allocatedWidth);
    }

    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + frameHeight));

    ImGui::PopID();
}

void LineEdit::renderImGuiConstrained(float width, float height)
{
    (void)height;
    renderWithAvailableWidth(width);
}

}  // namespace widgets
}  // namespace snf
