#include "SNFWidgets/SpinBox.h"

#include "imgui.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace snf {
namespace widgets {

namespace {
constexpr float k_defaultMinimumInputWidth = 48.0f;

void normalizeRange(int& minimum, int& maximum)
{
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
}

float clampedWidth(float width)
{
    return std::max(0.0f, width);
}

bool canHaveNegativeValue(int minimum)
{
    return minimum < 0;
}

int decimalInputFilter(ImGuiInputTextCallbackData* data)
{
    const auto character = static_cast<unsigned int>(data->EventChar);
    if (character >= static_cast<unsigned int>('0') && character <= static_cast<unsigned int>('9')) {
        return 0;
    }

    if (character == static_cast<unsigned int>('-')) {
        const bool allowNegative = data->UserData != nullptr && *static_cast<const bool*>(data->UserData);
        const bool alreadyHasMinus = data->Buf != nullptr && std::strchr(data->Buf, '-') != nullptr;
        if (allowNegative && data->CursorPos == 0 && ! alreadyHasMinus) {
            return 0;
        }
    }

    return 1;
}

}  // namespace

SpinBox::SpinBox(snf::Node* parent)
    : Widget(parent)
{
    syncBuffer();
}

SpinBox::SpinBox(int minimum, int maximum, snf::Node* parent)
    : Widget(parent), m_minimum(minimum), m_maximum(maximum)
{
    normalizeRange(m_minimum, m_maximum);
    m_value = m_minimum;
    syncBuffer();
}

SpinBox::SpinBox(const std::string& label, int minimum, int maximum, snf::Node* parent)
    : Widget(parent), m_label(label), m_minimum(minimum), m_maximum(maximum)
{
    normalizeRange(m_minimum, m_maximum);
    m_value = m_minimum;
    syncBuffer();
}

void SpinBox::setLabel(const std::string& label)
{
    m_label = label;
}

std::string SpinBox::label() const
{
    return m_label;
}

void SpinBox::setTextPlacement(TextPlacement placement)
{
    m_textPlacement = placement;
}

SpinBox::TextPlacement SpinBox::textPlacement() const
{
    return m_textPlacement;
}

void SpinBox::setButtonsVisible(bool visible)
{
    m_buttonsVisible = visible;
}

bool SpinBox::buttonsVisible() const
{
    return m_buttonsVisible;
}

void SpinBox::setMinimumInputWidth(float width)
{
    m_minimumInputWidth = clampedWidth(width);
}

float SpinBox::minimumInputWidth() const
{
    return m_minimumInputWidth;
}

void SpinBox::setPreferredInputWidth(float width)
{
    m_preferredInputWidth = clampedWidth(width);
}

float SpinBox::preferredInputWidth() const
{
    return m_preferredInputWidth;
}

void SpinBox::setValue(int value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
    syncBuffer();
}

int SpinBox::value() const
{
    return m_value;
}

void SpinBox::setRange(int minimum, int maximum)
{
    normalizeRange(minimum, maximum);
    m_minimum = minimum;
    m_maximum = maximum;
    m_value = std::clamp(m_value, m_minimum, m_maximum);
    syncBuffer();
}

int SpinBox::minimum() const
{
    return m_minimum;
}

int SpinBox::maximum() const
{
    return m_maximum;
}

void SpinBox::setStep(int step)
{
    if (step > 0) {
        m_step = step;
    }
}

int SpinBox::step() const
{
    return m_step;
}

Size SpinBox::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float width = std::max(m_preferredInputWidth, effectiveMinimumInputWidth());
    if (! m_label.empty() && m_textPlacement != TextPlacement::Hidden) {
        width += sideTextWidth() + spacing;
    }
    if (m_buttonsVisible) {
        width += buttonsWidth() + spacing;
    }

    return Size{width, ImGui::GetFrameHeight()};
}

void SpinBox::syncBuffer()
{
    const std::string text = std::to_string(m_value);
    if (m_buffer.size() < k_bufferCapacity) {
        m_buffer.resize(k_bufferCapacity, '\0');
    }

    std::fill(m_buffer.begin(), m_buffer.end(), '\0');
    const std::size_t copySize = std::min(text.size(), m_buffer.size() - 1);
    std::copy(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(copySize), m_buffer.begin());
}

void SpinBox::sanitizeBuffer()
{
    if (m_buffer.empty()) {
        return;
    }

    const bool allowNegative = canHaveNegativeValue(m_minimum);
    std::string filtered;
    filtered.reserve(m_buffer.size());

    for (std::size_t i = 0; i < m_buffer.size() && m_buffer[i] != '\0'; ++i) {
        const char character = m_buffer[i];
        if (character >= '0' && character <= '9') {
            filtered.push_back(character);
        } else if (character == '-' && allowNegative && filtered.empty()) {
            filtered.push_back(character);
        }
    }

    std::fill(m_buffer.begin(), m_buffer.end(), '\0');
    const std::size_t copySize = std::min(filtered.size(), m_buffer.size() - 1);
    std::copy(filtered.begin(), filtered.begin() + static_cast<std::ptrdiff_t>(copySize), m_buffer.begin());
}

bool SpinBox::commitBufferValue(bool emitSignal, bool normalizeInvalid)
{
    sanitizeBuffer();
    const std::string text(m_buffer.data());
    if (text.empty() || text == "-") {
        if (normalizeInvalid) {
            syncBuffer();
        }
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        if (normalizeInvalid) {
            syncBuffer();
        }
        return false;
    }

    const int previous = m_value;
    m_value = std::clamp(static_cast<int>(parsed), m_minimum, m_maximum);
    syncBuffer();
    if (emitSignal && m_value != previous) {
        valueChanged.emit(m_value);
        return true;
    }
    return m_value != previous;
}

bool SpinBox::applyStep(int direction)
{
    const int previous = m_value;
    const long long next = static_cast<long long>(m_value) + static_cast<long long>(direction) * m_step;
    const long long clamped = std::max(
        static_cast<long long>(m_minimum),
        std::min(static_cast<long long>(m_maximum), next));
    m_value = static_cast<int>(clamped);
    syncBuffer();

    if (m_value != previous) {
        valueChanged.emit(m_value);
        return true;
    }
    return false;
}

float SpinBox::sideTextWidth() const
{
    if (m_label.empty() || ImGui::GetCurrentContext() == nullptr) {
        return 0.0f;
    }
    return ImGui::CalcTextSize(m_label.c_str()).x;
}

float SpinBox::buttonsWidth() const
{
    if (! m_buttonsVisible || ImGui::GetCurrentContext() == nullptr) {
        return 0.0f;
    }

    const float buttonWidth = ImGui::GetFrameHeight();
    return buttonWidth * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x;
}

float SpinBox::effectiveMinimumInputWidth() const
{
    if (m_minimumInputWidth > 0.0f) {
        return m_minimumInputWidth;
    }

    if (ImGui::GetCurrentContext() == nullptr) {
        return k_defaultMinimumInputWidth;
    }

    const std::string minimumText = std::to_string(m_minimum);
    const std::string maximumText = std::to_string(m_maximum);
    const std::string valueText = std::to_string(m_value);
    const float numericWidth = std::max({
        ImGui::CalcTextSize(minimumText.c_str()).x,
        ImGui::CalcTextSize(maximumText.c_str()).x,
        ImGui::CalcTextSize(valueText.c_str()).x});
    const float paddedWidth = numericWidth + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
    return std::max(k_defaultMinimumInputWidth, paddedWidth);
}

bool SpinBox::renderSideText(float width) const
{
    if (m_label.empty() || width <= 0.0f) {
        return false;
    }

    const float frameHeight = ImGui::GetFrameHeight();
    const float textHeight = ImGui::CalcTextSize(m_label.c_str()).y;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textPos(pos.x, pos.y + std::max(0.0f, (frameHeight - textHeight) * 0.5f));
    const ImVec4 clip(pos.x, pos.y, pos.x + width, pos.y + frameHeight);
    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(),
        ImGui::GetFontSize(),
        textPos,
        ImGui::GetColorU32(ImGuiCol_Text),
        m_label.c_str(),
        m_label.c_str() + m_label.size(),
        0.0f,
        &clip);
    return true;
}

bool SpinBox::renderInput(float width)
{
    bool allowNegative = canHaveNegativeValue(m_minimum);
    const bool hasWidth = width > 0.0f;
    if (hasWidth) {
        ImGui::PushItemWidth(width);
    }

    const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_CharsDecimal |
        ImGuiInputTextFlags_CallbackCharFilter |
        ImGuiInputTextFlags_EnterReturnsTrue;

    const bool returned = ImGui::InputText(
        "##input",
        m_buffer.data(),
        m_buffer.size(),
        flags,
        decimalInputFilter,
        &allowNegative);

    if (hasWidth) {
        ImGui::PopItemWidth();
    }

    bool changed = false;
    if (ImGui::IsItemEdited() || returned) {
        changed = commitBufferValue(true, false);
    } else if (ImGui::IsItemDeactivatedAfterEdit()) {
        commitBufferValue(true, true);
    }

    return changed;
}

void SpinBox::renderWithAvailableWidth(float width)
{
    ImGui::PushID(this);

    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float frameHeight = ImGui::GetFrameHeight();
    const float available = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const bool hasSideText = ! m_label.empty() && m_textPlacement != TextPlacement::Hidden;
    const float textWidth = hasSideText ? sideTextWidth() : 0.0f;
    const float buttonWidth = m_buttonsVisible ? buttonsWidth() : 0.0f;
    const int gapCount = static_cast<int>(hasSideText) + static_cast<int>(m_buttonsVisible);
    const float gaps = spacing * static_cast<float>(gapCount);
    const float minimumInput = std::max(1.0f, effectiveMinimumInputWidth());
    float inputWidth = available > 0.0f
        ? std::max(minimumInput, available - textWidth - buttonWidth - gaps)
        : m_preferredInputWidth;
    if (m_preferredInputWidth > 0.0f && available > 0.0f) {
        inputWidth = std::min(inputWidth, std::max(minimumInput, m_preferredInputWidth));
    }

    const ImVec2 start = ImGui::GetCursorScreenPos();
    float x = start.x;

    if (hasSideText && m_textPlacement == TextPlacement::Left) {
        renderSideText(textWidth);
        x += textWidth + spacing;
    }

    ImGui::SetCursorScreenPos(ImVec2(x, start.y));
    renderInput(inputWidth);
    x += inputWidth;

    if (m_buttonsVisible) {
        x += spacing;
        const float singleButtonWidth = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        if (ImGui::Button("-", ImVec2(singleButtonWidth, frameHeight))) {
            applyStep(-1);
        }
        x += singleButtonWidth + spacing;
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        if (ImGui::Button("+", ImVec2(singleButtonWidth, frameHeight))) {
            applyStep(1);
        }
        x += singleButtonWidth;
    }

    if (hasSideText && m_textPlacement == TextPlacement::Right) {
        x += spacing;
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        renderSideText(textWidth);
    }

    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + frameHeight));
    ImGui::PopID();
}

void SpinBox::renderImGui()
{
    renderWithAvailableWidth(ImGui::GetContentRegionAvail().x);
}

void SpinBox::renderImGuiConstrained(float width, float height)
{
    (void)height;
    renderWithAvailableWidth(width);
}

}  // namespace widgets
}  // namespace snf
