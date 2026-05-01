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

int normalizedStretchItemFactor(int stretch)
{
    return std::max(1, stretch);
}

float normalizedSpacingSize(float pixels)
{
    return std::max(0.0f, pixels);
}

float defaultHorizontalSpacing()
{
    return ImGui::GetCurrentContext() ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
}

float defaultVerticalSpacing()
{
    return ImGui::GetCurrentContext() ? ImGui::GetStyle().ItemSpacing.y : 0.0f;
}
}  // namespace

Layout::Layout(snf::Node* parent) : Widget(parent) {}

void Layout::addWidget(Widget* widget, int stretch)
{
    if (! widget || widget == this) {
        return;
    }

    for (auto& item : m_items) {
        if (item.type == Layout::LayoutItemType::Widget && item.widget == widget) {
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

    Item item;
    item.type = LayoutItemType::Widget;
    item.widget = snf::NodePtr<Widget>(widget);
    item.stretch = normalizedStretch(stretch);
    m_items.push_back(item);
}

void Layout::addSpacing(float pixels)
{
    Item item;
    item.type = LayoutItemType::FixedSpacer;
    item.fixedSize = normalizedSpacingSize(pixels);
    m_items.push_back(item);
}

void Layout::addStretch(int factor)
{
    Item item;
    item.type = LayoutItemType::StretchSpacer;
    item.stretch = normalizedStretchItemFactor(factor);
    m_items.push_back(item);
}

void Layout::removeWidget(Widget* widget)
{
    m_items.erase(
        std::remove_if(
            m_items.begin(),
            m_items.end(),
            [widget](const Item& item) {
                return item.type == Layout::LayoutItemType::Widget && item.widget == widget;
            }),
        m_items.end());
}

void Layout::setStretch(Widget* widget, int stretch)
{
    for (auto& item : m_items) {
        if (item.type == LayoutItemType::Widget && item.widget == widget) {
            item.stretch = normalizedStretch(stretch);
            return;
        }
    }
}

int Layout::count() const
{
    return static_cast<int>(m_items.size());
}

Layout::LayoutItemType Layout::itemTypeAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return LayoutItemType::Widget;
    }
    return m_items[static_cast<std::size_t>(index)].type;
}

Widget* Layout::widgetAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return nullptr;
    }

    const auto& item = m_items[static_cast<std::size_t>(index)];
    if (item.type != LayoutItemType::Widget) {
        return nullptr;
    }

    const auto& widget = item.widget;
    return widget ? widget.get() : nullptr;
}

int Layout::stretchAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return 0;
    }
    return m_items[static_cast<std::size_t>(index)].stretch;
}

float Layout::fixedSpacingAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return 0.0f;
    }

    const auto& item = m_items[static_cast<std::size_t>(index)];
    return item.type == LayoutItemType::FixedSpacer ? item.fixedSize : 0.0f;
}

bool Layout::containsWidget(const Widget* widget) const
{
    if (! widget) {
        return false;
    }

    for (const auto& item : m_items) {
        if (item.type != LayoutItemType::Widget) {
            continue;
        }

        if (! item.widget) {
            continue;
        }

        Widget* itemWidget = item.widget.get();
        if (itemWidget == widget) {
            return true;
        }

        if (itemWidget->containsWidget(widget)) {
            return true;
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
        if (item.type != LayoutItemType::Widget) {
            active.push_back(item);
        } else if (item.widget && ! item.widget.isMarkedToDelete()) {
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
    widget->renderWidgetConstrained(width, height);
    ImGui::PopID();
}

void Layout::renderImGuiConstrained(float width, float height)
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const Size hint = sizeHint();

    const float childWidth = std::max(0.0f, width > 0.0f ? width : available.x);
    const float childHeight = std::max(0.0f, height > 0.0f ? height : hint.height);
    if (childWidth <= 0.0f || childHeight <= 0.0f) {
        renderImGui();
        return;
    }

    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushID(this);
    if (ImGui::BeginChild("layout_bounds", ImVec2(childWidth, childHeight), ImGuiChildFlags_None, childFlags)) {
        renderImGui();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

VBoxLayout::VBoxLayout(snf::Node* parent) : Layout(parent) {}

Size VBoxLayout::sizeHint() const
{
    const auto active = activeItems();
    if (active.empty()) {
        return {};
    }

    const float effectiveSpacing = spacing() >= 0.0f ? spacing() : defaultVerticalSpacing();
    float width = 0.0f;
    float height = 0.0f;

    for (const auto& item : active) {
        if (item.type == LayoutItemType::Widget) {
            const Size hint = item.widget ? item.widget->sizeHint() : Size{};
            width = std::max(width, hint.width);
            height += std::max(0.0f, hint.height);
        } else if (item.type == LayoutItemType::FixedSpacer) {
            height += item.fixedSize;
        }
    }

    height += effectiveSpacing * static_cast<float>(active.size() > 1 ? active.size() - 1 : 0);
    return Size{width, height};
}

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
    float fixedHeight = 0.0f;
    for (const auto& item : active) {
        if (item.type == LayoutItemType::Widget) {
            fixedHeight += item.widget ? item.widget->sizeHint().height : 0.0f;
            totalStretch += item.stretch;
        } else if (item.type == LayoutItemType::FixedSpacer) {
            fixedHeight += item.fixedSize;
        } else if (item.type == LayoutItemType::StretchSpacer) {
            totalStretch += item.stretch;
        }
    }

    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float spacingHeight = effectiveSpacing * static_cast<float>(active.size() > 1 ? active.size() - 1 : 0);
    const float stretchHeight = std::max(0.0f, availableHeight - fixedHeight - spacingHeight);

    for (const auto& item : active) {
        if (item.type == LayoutItemType::FixedSpacer) {
            ImGui::Dummy(ImVec2(0.0f, item.fixedSize));
            continue;
        }

        if (item.type == LayoutItemType::StretchSpacer) {
            const float height = totalStretch > 0
                ? stretchHeight * static_cast<float>(item.stretch) / static_cast<float>(totalStretch)
                : 0.0f;
            ImGui::Dummy(ImVec2(0.0f, height));
            continue;
        }

        Widget* widget = item.widget.get();
        const Size hint = widget ? widget->sizeHint() : Size{};
        const float extraHeight = (totalStretch > 0 && item.stretch > 0)
            ? stretchHeight * static_cast<float>(item.stretch) / static_cast<float>(totalStretch)
            : 0.0f;
        const float height = hint.height > 0.0f || extraHeight > 0.0f ? hint.height + extraHeight : -1.0f;
        renderWidget(widget, -1.0f, height);
    }

    if (spacing() >= 0.0f) {
        ImGui::PopStyleVar();
    }

    ImGui::PopID();
}

HBoxLayout::HBoxLayout(snf::Node* parent) : Layout(parent) {}

Size HBoxLayout::sizeHint() const
{
    const auto active = activeItems();
    if (active.empty()) {
        return {};
    }

    const float effectiveSpacing = spacing() >= 0.0f ? spacing() : defaultHorizontalSpacing();
    float width = 0.0f;
    float height = 0.0f;

    for (const auto& item : active) {
        if (item.type == LayoutItemType::Widget) {
            const Size hint = item.widget ? item.widget->sizeHint() : Size{};
            width += std::max(0.0f, hint.width);
            height = std::max(height, hint.height);
        } else if (item.type == LayoutItemType::FixedSpacer) {
            width += item.fixedSize;
        }
    }

    width += effectiveSpacing * static_cast<float>(active.size() > 1 ? active.size() - 1 : 0);
    return Size{width, height};
}

void HBoxLayout::renderImGui()
{
    const auto active = activeItems();
    if (active.empty()) {
        return;
    }

    ImGui::PushID(this);

    const float effectiveSpacing = spacing() >= 0.0f ? spacing() : ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float availableWidth = ImGui::GetContentRegionAvail().x;

    float fixedWidth = 0.0f;
    float maxHintHeight = 0.0f;
    int totalStretch = 0;
    for (const auto& item : active) {
        if (item.type == LayoutItemType::Widget) {
            Widget* widget = item.widget.get();
            const Size hint = widget ? widget->sizeHint() : Size{};
            fixedWidth += std::max(0.0f, hint.width);
            maxHintHeight = std::max(maxHintHeight, hint.height);
            totalStretch += item.stretch;
        } else if (item.type == LayoutItemType::FixedSpacer) {
            fixedWidth += item.fixedSize;
        } else if (item.type == LayoutItemType::StretchSpacer) {
            totalStretch += item.stretch;
        }
    }

    const float totalSpacing = effectiveSpacing * static_cast<float>(active.size() > 1 ? active.size() - 1 : 0);
    const float remainingWidth = std::max(0.0f, availableWidth - fixedWidth - totalSpacing);
    const float layoutHeight = maxHintHeight > 0.0f ? maxHintHeight : ImGui::GetFrameHeight();

    float x = start.x;
    for (const auto& item : active) {
        float itemWidth = 0.0f;
        if (item.type == LayoutItemType::Widget) {
            Widget* widget = item.widget.get();
            const Size hint = widget ? widget->sizeHint() : Size{};
            const float extraWidth = (totalStretch > 0 && item.stretch > 0)
                ? remainingWidth * static_cast<float>(item.stretch) / static_cast<float>(totalStretch)
                : 0.0f;
            itemWidth = std::max(0.0f, hint.width) + extraWidth;
            ImGui::SetCursorScreenPos(ImVec2(x, start.y));
            renderWidget(widget, itemWidth > 0.0f ? itemWidth : -1.0f, -1.0f);
        } else if (item.type == LayoutItemType::FixedSpacer) {
            itemWidth = item.fixedSize;
        } else if (item.type == LayoutItemType::StretchSpacer) {
            itemWidth = totalStretch > 0
                ? remainingWidth * static_cast<float>(item.stretch) / static_cast<float>(totalStretch)
                : 0.0f;
        }

        x += itemWidth + effectiveSpacing;
    }

    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + std::max(0.0f, layoutHeight)));
    ImGui::Dummy(ImVec2(std::max(0.0f, availableWidth), 0.0f));

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

Size FormLayout::sizeHint() const
{
    if (m_rows.empty()) {
        return {};
    }

    float labelWidth = 0.0f;
    float fieldWidth = 0.0f;
    float height = 0.0f;

    const float rowSpacing = defaultVerticalSpacing();
    const float columnSpacing = defaultHorizontalSpacing();
    const float rowHeight = ImGui::GetCurrentContext() ? ImGui::GetFrameHeight() : 0.0f;

    for (const auto& row : m_rows) {
        if (ImGui::GetCurrentContext() && ! row.label.empty()) {
            labelWidth = std::max(labelWidth, ImGui::CalcTextSize(row.label.c_str()).x);
        }

        const Size hint = row.widget ? row.widget->sizeHint() : Size{};
        fieldWidth = std::max(fieldWidth, hint.width);
        height += std::max(rowHeight, hint.height);
    }

    height += rowSpacing * static_cast<float>(m_rows.size() > 1 ? m_rows.size() - 1 : 0);
    return Size{labelWidth + columnSpacing + fieldWidth, height};
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

        if (rowWidget->containsWidget(widget)) {
            return true;
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
        ImGuiTableFlags_NoPadOuterX;

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
