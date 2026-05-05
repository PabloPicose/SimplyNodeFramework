#include "SNFWidgets/ComboBox.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

namespace {
std::string visibleLabel(const std::string& label)
{
    const std::size_t marker = label.find("##");
    return marker != std::string::npos ? label.substr(0, marker) : label;
}
}  // namespace

Size ComboBox::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const ImGuiStyle& style = ImGui::GetStyle();

    float maxItemWidth = 0.0f;
    for (const auto& item : m_items) {
        maxItemWidth = std::max(maxItemWidth, ImGui::CalcTextSize(item.c_str()).x);
    }

    // Combo body: preview text + frame padding + arrow button (square)
    const float arrowWidth = ImGui::GetFrameHeight();
    const float comboWidth = style.FramePadding.x * 2.0f + maxItemWidth + arrowWidth;

    // Visible label to the right (text before ##)
    float labelWidth = 0.0f;
    if (m_textPlacement != TextPlacement::Hidden) {
        const std::string labelText = visibleLabel(m_label);
        if (! labelText.empty()) {
            labelWidth = ImGui::CalcTextSize(labelText.c_str()).x + style.ItemInnerSpacing.x;
        }
    }

    return Size{comboWidth + labelWidth, ImGui::GetFrameHeight()};
}

ComboBox::ComboBox(snf::Node* parent)
    : Widget(parent)
{
}

ComboBox::ComboBox(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
}

void ComboBox::setLabel(const std::string& label)
{
    m_label = label;
}

std::string ComboBox::label() const
{
    return m_label;
}

void ComboBox::setTextPlacement(ComboBox::TextPlacement placement)
{
    m_textPlacement = placement;
}

ComboBox::TextPlacement ComboBox::textPlacement() const
{
    return m_textPlacement;
}

void ComboBox::addItem(const std::string& item)
{
    m_items.push_back(item);
    // Auto-select first item when the list was previously empty.
    if (m_currentIndex == -1) {
        m_currentIndex = 0;
    }
}

void ComboBox::addItems(const std::vector<std::string>& items)
{
    for (const auto& item : items) {
        addItem(item);
    }
}

void ComboBox::clearItems()
{
    m_items.clear();
    m_currentIndex = -1;
}

int ComboBox::count() const
{
    return static_cast<int>(m_items.size());
}

void ComboBox::setCurrentIndex(int index)
{
    if (index < -1 || index >= static_cast<int>(m_items.size())) {
        return;  // Out of range: silently ignored.
    }
    m_currentIndex = index;
}

int ComboBox::currentIndex() const
{
    return m_currentIndex;
}

std::string ComboBox::currentText() const
{
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_items.size())) {
        return {};
    }
    return m_items[static_cast<std::size_t>(m_currentIndex)];
}

void ComboBox::renderImGuiConstrained(float width, float height)
{
    (void)height;

    // Compute the minimum width needed so that all item texts fit without
    // clipping inside the popup window.  BeginCombo internally constrains the
    // popup to CalcItemWidth(), so we must ensure that value is large enough.
    float minWidth = width > 0.0f ? width : 0.0f;
    if (ImGui::GetCurrentContext() != nullptr) {
        const ImGuiStyle& style   = ImGui::GetStyle();
        const float       padding = style.FramePadding.x * 2.0f
                                    + style.ScrollbarSize;  // popup may have a scrollbar
        for (const auto& item : m_items) {
            minWidth = std::max(minWidth,
                                ImGui::CalcTextSize(item.c_str()).x + padding);
        }
    }

    if (minWidth > 0.0f) {
        ImGui::SetNextItemWidth(minWidth);
    }
    renderImGui();
}

void ComboBox::renderImGui()
{
    ImGui::PushID(this);
    const std::string displayLabel =
        (m_textPlacement == TextPlacement::Hidden) ? std::string{"##"} : m_label;

    if (m_items.empty()) {
        // Render a disabled combo when the list is empty.
        ImGui::BeginDisabled();
        const char* placeholder = "(empty)";
        ImGui::BeginCombo(displayLabel.c_str(), placeholder);
        ImGui::EndDisabled();
        ImGui::PopID();
        return;
    }

    const std::string prevText  = currentText();
    const int         prevIndex = m_currentIndex;

    const char* previewText = (m_currentIndex >= 0) ? m_items[static_cast<std::size_t>(m_currentIndex)].c_str() : "";

    if (ImGui::BeginCombo(displayLabel.c_str(), previewText)) {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            const bool selected = (i == m_currentIndex);
            if (ImGui::Selectable(m_items[static_cast<std::size_t>(i)].c_str(), selected)) {
                m_currentIndex = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::PopID();

    if (m_currentIndex != prevIndex) {
        currentIndexChanged.emit(m_currentIndex);
        currentTextChanged.emit(currentText());
    }
}

}  // namespace widgets
}  // namespace snf
