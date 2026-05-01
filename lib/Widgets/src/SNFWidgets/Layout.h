#pragma once

/**
 * @file Layout.h
 * @brief Qt-style ImGui-backed layout containers.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <SNFCore/NodePtr.h>

#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class Layout
 * @ingroup SNFWidgets
 * @brief Base class for non-visual containers that arrange child widgets.
 *
 * Layouts are widgets themselves, so they can be installed on a Window or
 * nested inside another layout.  `addWidget()` keeps a render-order entry with
 * an optional stretch factor.  Widgets without a parent are reparented to the
 * layout; widgets that already have a parent keep their existing owner.
 */
class Layout : public Widget
{
public:
    enum class LayoutItemType
    {
        Widget,
        FixedSpacer,
        StretchSpacer
    };

    explicit Layout(snf::Node* parent = nullptr);

    void addWidget(Widget* widget, int stretch = 0);
    void addSpacing(float pixels);
    void addStretch(int factor = 1);
    void removeWidget(Widget* widget);
    void setStretch(Widget* widget, int stretch);

    int count() const;
    LayoutItemType itemTypeAt(int index) const;
    Widget* widgetAt(int index) const;
    int stretchAt(int index) const;
    float fixedSpacingAt(int index) const;
    bool containsWidget(const Widget* widget) const override;

    void setSpacing(float spacing);
    float spacing() const;

protected:
    struct Item
    {
        LayoutItemType type = LayoutItemType::Widget;
        snf::NodePtr<Widget> widget{nullptr};
        int stretch = 0;
        float fixedSize = 0.0f;
    };

    const std::vector<Item>& items() const;
    std::vector<Item> activeItems() const;
    void renderWidget(Widget* widget, float width = -1.0f, float height = -1.0f);
    void renderImGuiConstrained(float width, float height) override;

private:
    friend class Window;

    std::vector<Item> m_items;
    float m_spacing = -1.0f;
};

/**
 * @class VBoxLayout
 * @ingroup SNFWidgets
 * @brief Renders child widgets from top to bottom.
 */
class VBoxLayout : public Layout
{
public:
    explicit VBoxLayout(snf::Node* parent = nullptr);

    Size sizeHint() const override;

protected:
    void renderImGui() override;
};

/**
 * @class HBoxLayout
 * @ingroup SNFWidgets
 * @brief Renders child widgets from left to right.
 */
class HBoxLayout : public Layout
{
public:
    explicit HBoxLayout(snf::Node* parent = nullptr);

    Size sizeHint() const override;

protected:
    void renderImGui() override;
};

/**
 * @class FormLayout
 * @ingroup SNFWidgets
 * @brief Two-column label/widget layout for simple forms.
 */
class FormLayout : public Layout
{
public:
    explicit FormLayout(snf::Node* parent = nullptr);

    void addRow(const std::string& label, Widget* widget, int stretch = 1);

    int rowCount() const;
    std::string rowLabelAt(int index) const;
    Widget* rowWidgetAt(int index) const;
    int rowStretchAt(int index) const;
    Size sizeHint() const override;
    bool containsWidget(const Widget* widget) const override;

protected:
    void renderImGui() override;

private:
    struct Row
    {
        std::string label;
        snf::NodePtr<Widget> widget;
        int stretch = 1;
    };

    std::vector<Row> m_rows;
};

}  // namespace widgets
}  // namespace snf
