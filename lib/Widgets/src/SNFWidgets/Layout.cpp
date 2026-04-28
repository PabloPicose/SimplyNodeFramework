#include "SNFWidgets/Layout.h"

#include "imgui.h"

#include <algorithm>
#include <string>

namespace snf {
namespace widgets {

namespace {
int normalizedStretch(int stretch)
{
    return std::max(0, stretch);
}
}  // namespace

Layout::Layout(snf::Node* parent) : Widget(parent) {}

void Layout::addWidget(Widget* widget, int stretch)
{
    if (! widget || widget == this) {
        return;
    }

    for (auto& item : m_items) {
        if (item.widget == widget) {
            item.stretch = normalizedStretch(stretch);
            if (! widget->parent()) {
                widget->setParent(this);
            }
            return;
        }
    }

    if (! widget->parent()) {
        widget->setParent(this);
    }

    m_items.push_back({snf::NodePtr<Widget>(widget), normalizedStretch(stretch)});
}

void Layout::removeWidget(Widget* widget)
{
    m_items.erase(
        std::remove_if(
            m_items.begin(),
            m_items.end(),
            [widget](const Item& item) { return item.widget == widget; }),
        m_items.end());
}

void Layout::setStretch(Widget* widget, int stretch)
{
    for (auto& item : m_items) {
        if (item.widget == widget) {
            item.stretch = normalizedStretch(stretch);
            return;
        }
    }
}

int Layout::count() const
{
    return static_cast<int>(m_items.size());
}

Widget* Layout::widgetAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return nullptr;
    }

    const auto& widget = m_items[static_cast<std::size_t>(index)].widget;
    return widget ? widget.get() : nullptr;
}

int Layout::stretchAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return 0;
    }
    return m_items[static_cast<std::size_t>(index)].stretch;
}

bool Layout::containsWidget(const Widget* widget) const
{
    if (! widget) {
        return false;
    }

    for (const auto& item : m_items) {
        if (! item.widget) {
            continue;
        }

        Widget* itemWidget = item.widget.get();
        if (itemWidget == widget) {
            return true;
        }

        if (const auto* nestedLayout = dynamic_cast<const Layout*>(itemWidget)) {
            if (nestedLayout->containsWidget(widget)) {
                return true;
            }
        }
    }

    return false;
}

void Layout::setSpacing(float spacing)
{
    m_spacing = spacing;
}

float Layout::spacing() const
{
    return m_spacing;
}

const std::vector<Layout::Item>& Layout::items() const
{
    return m_items;
}

std::vector<Layout::Item> Layout::activeItems() const
{
    std::vector<Item> active;
    active.reserve(m_items.size());

    for (const auto& item : m_items) {
        if (item.widget && ! item.widget.isMarkedToDelete()) {
            active.push_back(item);
        }
    }

    return active;
}

void Layout::renderWidget(Widget* widget, float width, float height)
{
    if (! widget) {
        return;
    }

    ImGui::PushID(widget);
    widget->renderImGuiConstrained(width, height);
    ImGui::PopID();
}

VBoxLayout::VBoxLayout(snf::Node* parent) : Layout(parent) {}

void VBoxLayout::renderImGui()
{
    const auto active = activeItems();
    if (active.empty()) {
        return;
    }

    ImGui::PushID(this);

    const float effectiveSpacing = spacing() >= 0.0f ? spacing() : ImGui::GetStyle().ItemSpacing.y;
    if (spacing() >= 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, spacing()));
    }

    int totalStretch = 0;
    for (const auto& item : active) {
        totalStretch += item.stretch;
    }

    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float spacingHeight = effectiveSpacing * static_cast<float>(active.size() > 1 ? active.size() - 1 : 0);
    const float stretchHeight = std::max(0.0f, availableHeight - spacingHeight);

    for (const auto& item : active) {
        Widget* widget = item.widget.get();
        const float height = (totalStretch > 0 && item.stretch > 0)
            ? stretchHeight * static_cast<float>(item.stretch) / static_cast<float>(totalStretch)
            : -1.0f;
        renderWidget(widget, -1.0f, height);
    }

    if (spacing() >= 0.0f) {
        ImGui::PopStyleVar();
    }

    ImGui::PopID();
}

HBoxLayout::HBoxLayout(snf::Node* parent) : Layout(parent) {}

void HBoxLayout::renderImGui()
{
    const auto active = activeItems();
    if (active.empty()) {
        return;
    }

    ImGui::PushID(this);

    const float effectiveSpacing = spacing() >= 0.0f ? spacing() : ImGui::GetStyle().ItemSpacing.x;
    if (spacing() >= 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing(), ImGui::GetStyle().ItemSpacing.y));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(effectiveSpacing * 0.5f, 0.0f));

    const ImGuiTableFlags flags =
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_NoPadOuterX;

    if (ImGui::BeginTable("HBoxLayout", static_cast<int>(active.size()), flags)) {
        for (std::size_t i = 0; i < active.size(); ++i) {
            const auto& item = active[i];
            const ImGuiTableColumnFlags columnFlags = item.stretch > 0
                ? ImGuiTableColumnFlags_WidthStretch
                : ImGuiTableColumnFlags_WidthFixed;
            const float widthOrWeight = item.stretch > 0 ? static_cast<float>(item.stretch) : 0.0f;
            const std::string columnId = "Column" + std::to_string(i);
            ImGui::TableSetupColumn(columnId.c_str(), columnFlags, widthOrWeight);
        }

        ImGui::TableNextRow();
        for (std::size_t i = 0; i < active.size(); ++i) {
            ImGui::TableSetColumnIndex(static_cast<int>(i));
            const auto& item = active[i];
            const float width = item.stretch > 0 ? ImGui::GetContentRegionAvail().x : -1.0f;
            renderWidget(item.widget.get(), width);
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();

    if (spacing() >= 0.0f) {
        ImGui::PopStyleVar();
    }

    ImGui::PopID();
}

FormLayout::FormLayout(snf::Node* parent) : Layout(parent) {}

void FormLayout::addRow(const std::string& label, Widget* widget, int stretch)
{
    if (! widget || widget == this) {
        return;
    }

    if (! widget->parent()) {
        widget->setParent(this);
    }

    m_rows.push_back({label, snf::NodePtr<Widget>(widget), normalizedStretch(stretch)});
}

int FormLayout::rowCount() const
{
    return static_cast<int>(m_rows.size());
}

std::string FormLayout::rowLabelAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_rows.size())) {
        return {};
    }
    return m_rows[static_cast<std::size_t>(index)].label;
}

Widget* FormLayout::rowWidgetAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }

    const auto& widget = m_rows[static_cast<std::size_t>(index)].widget;
    return widget ? widget.get() : nullptr;
}

int FormLayout::rowStretchAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_rows.size())) {
        return 0;
    }
    return m_rows[static_cast<std::size_t>(index)].stretch;
}

bool FormLayout::containsWidget(const Widget* widget) const
{
    if (! widget) {
        return false;
    }

    for (const auto& row : m_rows) {
        if (! row.widget) {
            continue;
        }

        Widget* rowWidget = row.widget.get();
        if (rowWidget == widget) {
            return true;
        }

        if (const auto* nestedLayout = dynamic_cast<const Layout*>(rowWidget)) {
            if (nestedLayout->containsWidget(widget)) {
                return true;
            }
        }
    }

    return false;
}

void FormLayout::renderImGui()
{
    if (m_rows.empty()) {
        return;
    }

    ImGui::PushID(this);

    const ImGuiTableFlags flags =
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_PadOuterX;

    if (ImGui::BeginTable("FormLayout", 2, flags)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);

        for (const auto& row : m_rows) {
            if (! row.widget || row.widget.isMarkedToDelete()) {
                continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (! row.label.empty()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(row.label.c_str());
            }

            ImGui::TableSetColumnIndex(1);
            const float width = row.stretch > 0 ? ImGui::GetContentRegionAvail().x : -1.0f;
            renderWidget(row.widget.get(), width);
        }

        ImGui::EndTable();
    }

    ImGui::PopID();
}

}  // namespace widgets
}  // namespace snf
