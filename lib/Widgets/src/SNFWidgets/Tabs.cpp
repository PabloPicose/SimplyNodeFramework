#include "SNFWidgets/Tabs.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

Tabs::Tabs(snf::Node* parent)
    : Widget(parent)
{
}

int Tabs::addTab(const std::string& title, Widget* page)
{
    if (! page || page == this) {
        return -1;
    }

    Tab tab;
    tab.title = title;
    setPageWidget(tab, page);
    m_tabs.push_back(tab);

    if (m_currentIndex < 0) {
        updateCurrentIndex(0, false);
        m_selectCurrentOnNextRender = true;
    }

    return static_cast<int>(m_tabs.size()) - 1;
}

void Tabs::removeTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        return;
    }

    Widget* page = widgetAt(index);
    if (page && page->parent() == this) {
        page->deleteLater();
    }

    m_tabs.erase(m_tabs.begin() + index);
    normalizeCurrentIndexAfterRemoval(index);
}

void Tabs::clear()
{
    for (int index = 0; index < static_cast<int>(m_tabs.size()); ++index) {
        Widget* page = widgetAt(index);
        if (page && page->parent() == this) {
            page->deleteLater();
        }
    }

    m_tabs.clear();
    updateCurrentIndex(-1, true);
}

int Tabs::count() const
{
    return static_cast<int>(m_tabs.size());
}

std::string Tabs::tabTitleAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        return {};
    }
    return m_tabs[static_cast<std::size_t>(index)].title;
}

void Tabs::setTabTitle(int index, const std::string& title)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        return;
    }
    m_tabs[static_cast<std::size_t>(index)].title = title;
}

Widget* Tabs::widgetAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        return nullptr;
    }

    const auto& page = m_tabs[static_cast<std::size_t>(index)].page;
    return page && ! page.isMarkedToDelete() ? page.get() : nullptr;
}

void Tabs::setCurrentIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        return;
    }

    updateCurrentIndex(index, true);
    m_selectCurrentOnNextRender = true;
}

int Tabs::currentIndex() const
{
    return m_currentIndex;
}

Size Tabs::sizeHint() const
{
    float width = 0.0f;
    float height = 0.0f;

    for (int index = 0; index < static_cast<int>(m_tabs.size()); ++index) {
        Widget* page = widgetAt(index);
        if (! page) {
            continue;
        }

        const Size hint = page->sizeHint();
        width = std::max(width, hint.width);
        height = std::max(height, hint.height);
    }

    if (ImGui::GetCurrentContext() != nullptr) {
        height += ImGui::GetFrameHeightWithSpacing();
    }

    return Size{width, height};
}

bool Tabs::containsWidget(const Widget* widget) const
{
    if (! widget) {
        return false;
    }

    for (int index = 0; index < static_cast<int>(m_tabs.size()); ++index) {
        Widget* page = widgetAt(index);
        if (! page) {
            continue;
        }

        if (page == widget) {
            return true;
        }

        if (page->containsWidget(widget)) {
            return true;
        }
    }

    return false;
}

void Tabs::renderImGui()
{
    renderWithAvailableSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
}

void Tabs::renderImGuiConstrained(float width, float height)
{
    renderWithAvailableSize(width, height);
}

void Tabs::renderWithAvailableSize(float width, float height)
{
    if (width <= 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }
    if (height <= 0.0f) {
        height = ImGui::GetContentRegionAvail().y;
    }

    width = std::max(0.0f, width);
    height = std::max(0.0f, height);

    ImGui::PushID(this);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("tabs_root", ImVec2(width, height), ImGuiChildFlags_None, childFlags)) {
        renderTabBar();
        renderCurrentPage();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void Tabs::renderTabBar()
{
    if (m_tabs.empty()) {
        return;
    }

    const bool forcingSelection = m_selectCurrentOnNextRender;
    int selectedIndex = m_currentIndex;
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(m_tabs.size())) {
        selectedIndex = 0;
    }

    if (ImGui::BeginTabBar("tabs")) {
        for (int index = 0; index < static_cast<int>(m_tabs.size()); ++index) {
            Widget* page = widgetAt(index);
            if (! page) {
                continue;
            }

            ImGui::PushID(index);
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
            if (m_selectCurrentOnNextRender && index == m_currentIndex) {
                flags |= ImGuiTabItemFlags_SetSelected;
            }

            if (ImGui::BeginTabItem(m_tabs[static_cast<std::size_t>(index)].title.c_str(), nullptr, flags)) {
                selectedIndex = index;
                ImGui::EndTabItem();
            }
            ImGui::PopID();
        }
        ImGui::EndTabBar();
    }

    if (forcingSelection) {
        m_selectCurrentOnNextRender = selectedIndex != m_currentIndex;
        return;
    }

    m_selectCurrentOnNextRender = false;
    if (selectedIndex != m_currentIndex) {
        updateCurrentIndex(selectedIndex, true);
    }
}

void Tabs::renderCurrentPage()
{
    Widget* page = widgetAt(m_currentIndex);
    if (! page) {
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(0.0f, available.x);
    available.y = std::max(0.0f, available.y);
    if (available.x <= 0.0f || available.y <= 0.0f) {
        return;
    }

    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("tab_content", available, ImGuiChildFlags_None, childFlags)) {
        page->renderWidgetConstrained(available.x, available.y);
    }
    ImGui::EndChild();
}

void Tabs::setPageWidget(Tab& tab, Widget* page)
{
    if (page->parent() != this) {
        page->setParent(this);
    }
    tab.page = snf::NodePtr<Widget>(page);
}

void Tabs::updateCurrentIndex(int index, bool emitSignal)
{
    if (index < -1 || index >= static_cast<int>(m_tabs.size())) {
        return;
    }

    if (m_currentIndex == index) {
        return;
    }

    m_currentIndex = index;
    if (emitSignal) {
        currentChanged.emit(m_currentIndex);
    }
}

void Tabs::normalizeCurrentIndexAfterRemoval(int removedIndex)
{
    if (m_tabs.empty()) {
        updateCurrentIndex(-1, true);
        m_selectCurrentOnNextRender = false;
        return;
    }

    int newIndex = m_currentIndex;
    if (newIndex > removedIndex) {
        --newIndex;
    } else if (newIndex == removedIndex) {
        newIndex = std::min(removedIndex, static_cast<int>(m_tabs.size()) - 1);
    }

    if (newIndex < 0) {
        newIndex = 0;
    }

    updateCurrentIndex(newIndex, true);
    m_selectCurrentOnNextRender = true;
}

}  // namespace widgets
}  // namespace snf
