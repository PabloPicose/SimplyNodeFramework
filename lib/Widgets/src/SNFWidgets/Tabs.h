#pragma once

/**
 * @file Tabs.h
 * @brief Tabbed container widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <SNFCore/Connection.h>
#include <SNFCore/NodePtr.h>

#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class Tabs
 * @ingroup SNFWidgets
 * @brief Container that shows one child page at a time behind a tab bar.
 *
 * `Tabs` owns the page widgets through the regular node parent/child
 * relationship. Pages are rendered only while their tab is selected.
 */
class Tabs : public Widget
{
public:
    explicit Tabs(snf::Node* parent = nullptr);

    int addTab(const std::string& title, Widget* page);
    void removeTab(int index);
    void clear();

    int count() const;
    std::string tabTitleAt(int index) const;
    void setTabTitle(int index, const std::string& title);
    Widget* widgetAt(int index) const;

    void setCurrentIndex(int index);
    int currentIndex() const;

    Size sizeHint() const override;
    bool containsWidget(const Widget* widget) const override;

    Signal<int> currentChanged;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    struct Tab
    {
        std::string title;
        snf::NodePtr<Widget> page{nullptr};
    };

    void renderWithAvailableSize(float width, float height);
    void renderTabBar();
    void renderCurrentPage();
    void setPageWidget(Tab& tab, Widget* page);
    void updateCurrentIndex(int index, bool emitSignal);
    void normalizeCurrentIndexAfterRemoval(int removedIndex);

    std::vector<Tab> m_tabs;
    int m_currentIndex = -1;
    bool m_selectCurrentOnNextRender = false;
};

}  // namespace widgets
}  // namespace snf
