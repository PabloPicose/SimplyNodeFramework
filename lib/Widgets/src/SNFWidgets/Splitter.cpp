#include "SNFWidgets/Splitter.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

namespace {
float clampedSize(float size)
{
    return std::max(0.0f, size);
}

float clampedRatio(float ratio)
{
    return std::clamp(ratio, 0.0f, 1.0f);
}

ImVec2 paneSize(Splitter::Orientation orientation, float alongAxis, float crossAxis)
{
    if (orientation == Splitter::Orientation::Horizontal) {
        return ImVec2(alongAxis, crossAxis);
    }
    return ImVec2(crossAxis, alongAxis);
}

ImGuiMouseCursor resizeCursor(Splitter::Orientation orientation)
{
    return orientation == Splitter::Orientation::Horizontal
        ? ImGuiMouseCursor_ResizeEW
        : ImGuiMouseCursor_ResizeNS;
}
}  // namespace

Splitter::Splitter(Orientation orientation, snf::Node* parent)
    : Widget(parent), m_orientation(orientation)
{
}

void Splitter::setOrientation(Orientation orientation)
{
    m_orientation = orientation;
}

Splitter::Orientation Splitter::orientation() const
{
    return m_orientation;
}

void Splitter::setPrimaryWidget(Widget* widget)
{
    setPaneWidget(m_primary, widget);
}

void Splitter::setSecondaryWidget(Widget* widget)
{
    setPaneWidget(m_secondary, widget);
}

void Splitter::setWidget(int index, Widget* widget)
{
    if (index == 0) {
        setPrimaryWidget(widget);
    } else if (index == 1) {
        setSecondaryWidget(widget);
    }
}

Widget* Splitter::primaryWidget() const
{
    return m_primary && ! m_primary.isMarkedToDelete() ? m_primary.get() : nullptr;
}

Widget* Splitter::secondaryWidget() const
{
    return m_secondary && ! m_secondary.isMarkedToDelete() ? m_secondary.get() : nullptr;
}

Widget* Splitter::widget(int index) const
{
    if (index == 0) {
        return primaryWidget();
    }
    if (index == 1) {
        return secondaryWidget();
    }
    return nullptr;
}

void Splitter::setRatio(float ratio)
{
    m_ratio = clampedRatio(ratio);
}

void Splitter::setInitialRatio(float ratio)
{
    setRatio(ratio);
}

float Splitter::ratio() const
{
    return m_ratio;
}

void Splitter::setSizes(float primarySize, float secondarySize)
{
    primarySize = clampedSize(primarySize);
    secondarySize = clampedSize(secondarySize);
    const float total = primarySize + secondarySize;
    if (total <= 0.0f) {
        return;
    }
    m_ratio = clampedRatio(primarySize / total);
    m_lastPrimarySize = primarySize;
    m_lastSecondarySize = secondarySize;
}

void Splitter::setInitialSizes(float primarySize, float secondarySize)
{
    setSizes(primarySize, secondarySize);
}

float Splitter::primarySize() const
{
    return m_lastPrimarySize;
}

float Splitter::secondarySize() const
{
    return m_lastSecondarySize;
}

void Splitter::setMinimumSizes(float primaryMinimum, float secondaryMinimum)
{
    m_primaryMinimumSize = clampedSize(primaryMinimum);
    m_secondaryMinimumSize = clampedSize(secondaryMinimum);
}

void Splitter::setPrimaryMinimumSize(float size)
{
    m_primaryMinimumSize = clampedSize(size);
}

void Splitter::setSecondaryMinimumSize(float size)
{
    m_secondaryMinimumSize = clampedSize(size);
}

float Splitter::primaryMinimumSize() const
{
    return m_primaryMinimumSize;
}

float Splitter::secondaryMinimumSize() const
{
    return m_secondaryMinimumSize;
}

void Splitter::setHandleSize(float size)
{
    m_handleSize = std::max(1.0f, size);
}

float Splitter::handleSize() const
{
    return m_handleSize;
}

Size Splitter::sizeHint() const
{
    const Size primaryHint = primaryWidget() ? primaryWidget()->sizeHint() : Size{};
    const Size secondaryHint = secondaryWidget() ? secondaryWidget()->sizeHint() : Size{};
    const float handle = effectiveHandleSize();

    if (m_orientation == Orientation::Horizontal) {
        return Size{
            primaryHint.width + secondaryHint.width + handle,
            std::max(primaryHint.height, secondaryHint.height)};
    }

    return Size{
        std::max(primaryHint.width, secondaryHint.width),
        primaryHint.height + secondaryHint.height + handle};
}

void Splitter::setPaneWidget(snf::NodePtr<Widget>& pane, Widget* widget)
{
    if (! widget || widget == this) {
        pane = snf::NodePtr<Widget>(nullptr);
        return;
    }

    if (widget->parent() != this) {
        widget->setParent(this);
    }

    pane = snf::NodePtr<Widget>(widget);
}

float Splitter::effectiveHandleSize() const
{
    return std::max(1.0f, m_handleSize);
}

float Splitter::clampPrimarySize(float desiredPrimarySize, float totalPaneSize) const
{
    totalPaneSize = std::max(0.0f, totalPaneSize);
    if (totalPaneSize <= 0.0f) {
        return 0.0f;
    }

    const float primaryMinimum = std::min(m_primaryMinimumSize, totalPaneSize);
    const float secondaryMinimum = std::min(m_secondaryMinimumSize, totalPaneSize);
    const float minimumSum = primaryMinimum + secondaryMinimum;
    if (minimumSum > totalPaneSize && minimumSum > 0.0f) {
        return totalPaneSize * (primaryMinimum / minimumSum);
    }

    const float minPrimary = primaryMinimum;
    const float maxPrimary = std::max(minPrimary, totalPaneSize - secondaryMinimum);
    return std::clamp(desiredPrimarySize, minPrimary, maxPrimary);
}

Splitter::Geometry Splitter::calculateGeometry(float width, float height) const
{
    width = std::max(0.0f, width);
    height = std::max(0.0f, height);

    const float axisSize = m_orientation == Orientation::Horizontal ? width : height;
    const float handle = std::min(effectiveHandleSize(), axisSize);
    const float totalPaneSize = std::max(0.0f, axisSize - handle);
    const float primary = clampPrimarySize(totalPaneSize * m_ratio, totalPaneSize);

    Geometry geometry;
    geometry.primarySize = primary;
    geometry.secondarySize = std::max(0.0f, totalPaneSize - primary);
    geometry.handleOffset = primary;
    geometry.handleSize = handle;
    geometry.totalPaneSize = totalPaneSize;
    return geometry;
}

void Splitter::updateRatioFromPrimarySize(float primarySize, float totalPaneSize)
{
    if (totalPaneSize <= 0.0f) {
        m_ratio = 0.0f;
        m_lastPrimarySize = 0.0f;
        m_lastSecondarySize = 0.0f;
        return;
    }

    const float clampedPrimary = clampPrimarySize(primarySize, totalPaneSize);
    m_ratio = clampedRatio(clampedPrimary / totalPaneSize);
    m_lastPrimarySize = clampedPrimary;
    m_lastSecondarySize = std::max(0.0f, totalPaneSize - clampedPrimary);
}

void Splitter::renderPane(const char* id, Widget* widget, float width, float height)
{
    width = std::max(0.0f, width);
    height = std::max(0.0f, height);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_None, flags)) {
        if (widget && ! snf::NodePtr<Widget>(widget).isMarkedToDelete()) {
            widget->renderWidgetConstrained(width, height);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void Splitter::renderWithAvailableSize(float width, float height)
{
    if (width <= 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }
    if (height <= 0.0f) {
        height = ImGui::GetContentRegionAvail().y;
    }

    width = std::max(0.0f, width);
    height = std::max(0.0f, height);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const Geometry geometry = calculateGeometry(width, height);
    m_lastPrimarySize = geometry.primarySize;
    m_lastSecondarySize = geometry.secondarySize;

    const ImVec2 primarySizeVec = paneSize(m_orientation, geometry.primarySize, m_orientation == Orientation::Horizontal ? height : width);
    const ImVec2 secondarySizeVec = paneSize(m_orientation, geometry.secondarySize, m_orientation == Orientation::Horizontal ? height : width);
    const ImVec2 handleSizeVec = paneSize(m_orientation, geometry.handleSize, m_orientation == Orientation::Horizontal ? height : width);

    ImGui::PushID(this);

    ImGui::SetCursorScreenPos(start);
    renderPane("primary", primaryWidget(), primarySizeVec.x, primarySizeVec.y);

    const ImVec2 handlePos = m_orientation == Orientation::Horizontal
        ? ImVec2(start.x + geometry.handleOffset, start.y)
        : ImVec2(start.x, start.y + geometry.handleOffset);
    ImGui::SetCursorScreenPos(handlePos);
    ImGui::InvisibleButton("handle", handleSizeVec);

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) {
        ImGui::SetMouseCursor(resizeCursor(m_orientation));
    }
    if (active) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        const float axisDelta = m_orientation == Orientation::Horizontal ? delta.x : delta.y;
        updateRatioFromPrimarySize(geometry.primarySize + axisDelta, geometry.totalPaneSize);
    }

    const ImU32 color = ImGui::GetColorU32(
        active ? ImGuiCol_SeparatorActive : (hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax(),
        color,
        0.0f);

    const ImVec2 secondaryPos = m_orientation == Orientation::Horizontal
        ? ImVec2(handlePos.x + geometry.handleSize, start.y)
        : ImVec2(start.x, handlePos.y + geometry.handleSize);
    ImGui::SetCursorScreenPos(secondaryPos);
    renderPane("secondary", secondaryWidget(), secondarySizeVec.x, secondarySizeVec.y);

    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));
    ImGui::PopID();
}

void Splitter::renderImGui()
{
    renderWithAvailableSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
}

void Splitter::renderImGuiConstrained(float width, float height)
{
    renderWithAvailableSize(width, height);
}

}  // namespace widgets
}  // namespace snf
