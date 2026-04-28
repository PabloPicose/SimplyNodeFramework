#include "SNFWidgets/ComboBox.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

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

void ComboBox::renderImGui()
{
    if (m_items.empty()) {
        // Render a disabled combo when the list is empty.
        ImGui::BeginDisabled();
        const char* placeholder = "(empty)";
        ImGui::BeginCombo(m_label.c_str(), placeholder);
        ImGui::EndDisabled();
        return;
    }

    const std::string prevText  = currentText();
    const int         prevIndex = m_currentIndex;

    const char* previewText = (m_currentIndex >= 0) ? m_items[static_cast<std::size_t>(m_currentIndex)].c_str() : "";

    if (ImGui::BeginCombo(m_label.c_str(), previewText)) {
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

    if (m_currentIndex != prevIndex) {
        currentIndexChanged.emit(m_currentIndex);
        currentTextChanged.emit(currentText());
    }
}

}  // namespace widgets
}  // namespace snf
