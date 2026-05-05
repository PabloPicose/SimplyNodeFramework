#pragma once

/**
 * @file StackedWidget.h
 * @brief Stack-of-pages container widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <SNFCore/Connection.h>
#include <SNFCore/NodePtr.h>

#include <vector>

namespace snf {
namespace widgets {

/**
 * @class StackedWidget
 * @ingroup SNFWidgets
 * @brief Container that shows exactly one child widget (page) at a time.
 *
 * Unlike `Tabs`, `StackedWidget` has no built-in navigation UI. The active
 * page is selected entirely through `setCurrentIndex()` or
 * `setCurrentWidget()`, making it suitable for wizard-style interfaces,
 * custom navigation bars, or any scenario where the switch mechanism is
 * provided by the surrounding application.
 *
 * All pages are owned through the regular node parent/child relationship.
 * Only the current page is rendered each frame; the others are fully
 * suppressed (no ImGui calls are made for them).
 *
 * @code
 * snf::widgets::StackedWidget stack(&window);
 * auto* page0 = new snf::widgets::Label("Page 0", &stack);
 * auto* page1 = new snf::widgets::Label("Page 1", &stack);
 * stack.addWidget(page0);
 * stack.addWidget(page1);
 * stack.setCurrentIndex(1);  // shows page1
 * @endcode
 */
class StackedWidget : public Widget
{
public:
    explicit StackedWidget(snf::Node* parent = nullptr);

    /**
     * @brief Appends @p widget as a new page and returns its index.
     *
     * If @p widget has no parent, it is re-parented to this `StackedWidget`.
     * Returns -1 if @p widget is null or equal to `this`.
     */
    int addWidget(Widget* widget);

    /**
     * @brief Removes the page containing @p widget.
     *
     * If the widget was parented to this `StackedWidget`, it is scheduled for
     * deletion. If @p widget is not a registered page, this is a no-op.
     */
    void removeWidget(Widget* widget);

    /** @brief Removes all pages and schedules owned widgets for deletion. */
    void clear();

    /** @brief Returns the number of pages. */
    int count() const;

    /**
     * @brief Returns the page widget at @p index, or nullptr if out of range
     *        or already deleted.
     */
    Widget* widgetAt(int index) const;

    /**
     * @brief Returns the index of @p widget, or -1 if not found.
     */
    int indexOf(const Widget* widget) const;

    /**
     * @brief Makes the page at @p index the currently visible one.
     *
     * Out-of-range values are ignored.
     */
    void setCurrentIndex(int index);

    /** @brief Returns the index of the currently visible page, or -1. */
    int currentIndex() const;

    /**
     * @brief Convenience overload — makes the page containing @p widget
     *        the currently visible one.
     *
     * Equivalent to `setCurrentIndex(indexOf(widget))`. Does nothing if
     * @p widget is not a registered page.
     */
    void setCurrentWidget(Widget* widget);

    /**
     * @brief Returns the currently visible page widget, or nullptr.
     */
    Widget* currentWidget() const;

    Size sizeHint() const override;
    bool containsWidget(const Widget* widget) const override;

    /** @brief Emitted whenever the current page index changes. */
    Signal<int> currentChanged;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    struct Page
    {
        snf::NodePtr<Widget> widget{nullptr};
    };

    std::vector<Page> m_pages;
    int m_currentIndex = -1;

    void setPageWidget(Page& page, Widget* widget);
    void updateCurrentIndex(int index, bool emitSignal);
    void normalizeCurrentIndexAfterRemoval(int removedIndex);
    void renderWithAvailableSize(float width, float height);
};

}  // namespace widgets
}  // namespace snf
