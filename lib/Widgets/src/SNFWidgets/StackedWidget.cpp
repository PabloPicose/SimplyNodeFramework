#include "SNFWidgets/StackedWidget.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

StackedWidget::StackedWidget(snf::Node* parent)
    : Widget(parent)
{
}

int StackedWidget::addWidget(Widget* widget)
{
    if (! widget || widget == this) {
        return -1;
    }

    Page page;
    setPageWidget(page, widget);
    m_pages.push_back(std::move(page));

    if (m_currentIndex < 0) {
        updateCurrentIndex(0, false);
    }

    return static_cast<int>(m_pages.size()) - 1;
}

void StackedWidget::removeWidget(Widget* widget)
{
    const int index = indexOf(widget);
    if (index < 0) {
        return;
    }

    if (widget->parent() == this) {
        widget->deleteLater();
    }

    m_pages.erase(m_pages.begin() + index);
    normalizeCurrentIndexAfterRemoval(index);
}

void StackedWidget::clear()
{
    for (auto& page : m_pages) {
        if (page.widget && ! page.widget.isMarkedToDelete()) {
            Widget* w = page.widget.get();
            if (w->parent() == this) {
                w->deleteLater();
            }
        }
    }

    m_pages.clear();
    updateCurrentIndex(-1, true);
}

int StackedWidget::count() const
{
    return static_cast<int>(m_pages.size());
}

Widget* StackedWidget::widgetAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return nullptr;
    }

    const auto& page = m_pages[static_cast<std::size_t>(index)].widget;
    return page && ! page.isMarkedToDelete() ? page.get() : nullptr;
}

int StackedWidget::indexOf(const Widget* widget) const
{
    if (! widget) {
        return -1;
    }

    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i) {
        if (widgetAt(i) == widget) {
            return i;
        }
    }

    return -1;
}

void StackedWidget::setCurrentIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return;
    }

    updateCurrentIndex(index, true);
}

int StackedWidget::currentIndex() const
{
    return m_currentIndex;
}

void StackedWidget::setCurrentWidget(Widget* widget)
{
    setCurrentIndex(indexOf(widget));
}

Widget* StackedWidget::currentWidget() const
{
    return widgetAt(m_currentIndex);
}

Size StackedWidget::sizeHint() const
{
    float width = 0.0f;
    float height = 0.0f;

    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i) {
        Widget* w = widgetAt(i);
        if (! w) {
            continue;
        }

        const Size hint = w->sizeHint();
        width = std::max(width, hint.width);
        height = std::max(height, hint.height);
    }

    return Size{width, height};
}

bool StackedWidget::containsWidget(const Widget* widget) const
{
    if (! widget) {
        return false;
    }

    for (int i = 0; i < static_cast<int>(m_pages.size()); ++i) {
        Widget* w = widgetAt(i);
        if (! w) {
            continue;
        }

        if (w == widget) {
            return true;
        }

        if (w->containsWidget(widget)) {
            return true;
        }
    }

    return false;
}

void StackedWidget::renderImGui()
{
    renderWithAvailableSize(ImGui::GetContentRegionAvail().x,
                            ImGui::GetContentRegionAvail().y);
}

void StackedWidget::renderImGuiConstrained(float width, float height)
{
    renderWithAvailableSize(width, height);
}

void StackedWidget::renderWithAvailableSize(float width, float height)
{
    Widget* page = widgetAt(m_currentIndex);
    if (! page) {
        return;
    }

    if (width <= 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }
    if (height <= 0.0f) {
        height = ImGui::GetContentRegionAvail().y;
    }

    width = std::max(0.0f, width);
    height = std::max(0.0f, height);

    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    ImGui::PushID(this);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("stacked_content", ImVec2(width, height), ImGuiChildFlags_None, childFlags)) {
        page->renderWidgetConstrained(width, height);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void StackedWidget::setPageWidget(Page& page, Widget* widget)
{
    if (widget->parent() != this) {
        widget->setParent(this);
    }
    page.widget = snf::NodePtr<Widget>(widget);
}

void StackedWidget::updateCurrentIndex(int index, bool emitSignal)
{
    if (index < -1 || index >= static_cast<int>(m_pages.size())) {
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

void StackedWidget::normalizeCurrentIndexAfterRemoval(int removedIndex)
{
    if (m_pages.empty()) {
        updateCurrentIndex(-1, true);
        return;
    }

    int newIndex = m_currentIndex;
    if (newIndex > removedIndex) {
        --newIndex;
    } else if (newIndex == removedIndex) {
        newIndex = std::min(removedIndex, static_cast<int>(m_pages.size()) - 1);
    }

    if (newIndex < 0) {
        newIndex = 0;
    }

    updateCurrentIndex(newIndex, true);
}

}  // namespace widgets
}  // namespace snf
