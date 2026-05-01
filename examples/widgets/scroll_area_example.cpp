#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Application.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/ScrollArea.h>
#include <SNFWidgets/Slider.h>
#include <SNFWidgets/Splitter.h>
#include <SNFWidgets/TableView.h>
#include <SNFWidgets/Window.h>

#include <string>
#include <utility>

namespace wg = snf::widgets;

class TwentyRowModel final : public snf::AbstractTableModel
{
public:
    explicit TwentyRowModel(std::string prefix)
        : m_prefix(std::move(prefix))
    {
    }

    int rowCount() const override
    {
        return 20;
    }

    int columnCount() const override
    {
        return 6;
    }

    std::string data(int row, int column) const override
    {
        if (row < 0 || row >= rowCount() || column < 0 || column >= columnCount()) {
            return {};
        }

        switch (column) {
        case 0:
            return m_prefix + "-" + std::to_string(row + 1);
        case 1:
            return "Long row label " + std::to_string(row + 1);
        case 2:
            return std::to_string((row + 1) * 7);
        case 3:
            return row % 3 == 0 ? "Queued" : "Ready";
        case 4:
            return "This column makes horizontal scrolling visible";
        case 5:
            return "Extra data " + std::to_string(1000 + row);
        default:
            return {};
        }
    }

    std::string headerData(int section) const override
    {
        switch (section) {
        case 0: return "Id";
        case 1: return "Name";
        case 2: return "Value";
        case 3: return "State";
        case 4: return "Description";
        case 5: return "Extra";
        default: return {};
        }
    }

private:
    std::string m_prefix;
};

int main()
{
    snf::Application app(0, nullptr);
    TwentyRowModel leftModel("A");
    TwentyRowModel rightModel("B");

    auto* webApp = new wg::ApplicationNode();
    webApp->setTitle("SNF ScrollArea Example");

    auto* window = new wg::Window("ScrollArea A/B demo", webApp);
    window->setInitialSize(980.0f, 620.0f);
    window->setInitialPosition(28.0f, 28.0f);
    window->setResizable(true);

    auto* mainLayout = new wg::VBoxLayout(window);
    window->setLayout(mainLayout);

    auto* splitter = new wg::Splitter(wg::Splitter::Orientation::Horizontal, window);
    splitter->setInitialRatio(0.5f);
    splitter->setMinimumSizes(80.0f, 80.0f);
    mainLayout->addWidget(splitter, 1);

    auto* leftPanel = new wg::VBoxLayout(window);
    splitter->setPrimaryWidget(leftPanel);

    auto* leftTitle = new wg::Label("A - TableView without ScrollArea", window);
    leftPanel->addWidget(leftTitle);

    auto* leftSlider = new wg::Slider("A", 0, 100, window);
    leftSlider->setValue(35);
    leftPanel->addWidget(leftSlider);

    auto* leftTable = new wg::TableView(window);
    leftTable->setModel(&leftModel);
    leftTable->setSelectionBehavior(wg::TableSelectionBehavior::Rows);
    leftTable->setStretchLastColumn(true);
    leftPanel->addWidget(leftTable, 1);

    auto* rightPanel = new wg::VBoxLayout(window);
    splitter->setSecondaryWidget(rightPanel);

    auto* rightTitle = new wg::Label("B - TableView inside ScrollArea", window);
    rightPanel->addWidget(rightTitle);

    auto* rightSlider = new wg::Slider("B", 0, 100, window);
    rightSlider->setValue(70);
    rightPanel->addWidget(rightSlider);

    auto* scrollArea = new wg::ScrollArea(window);
    scrollArea->setWidgetResizable(true);
    scrollArea->setVerticalScrollBarPolicy(wg::ScrollArea::ScrollBarPolicy::AsNeeded);
    scrollArea->setHorizontalScrollBarPolicy(wg::ScrollArea::ScrollBarPolicy::AsNeeded);
    rightPanel->addWidget(scrollArea, 1);

    auto* rightTable = new wg::TableView(window);
    rightTable->setModel(&rightModel);
    rightTable->setSelectionBehavior(wg::TableSelectionBehavior::Rows);
    rightTable->setStretchLastColumn(true);
    scrollArea->setWidget(rightTable);

    auto* isolatedWindow = new wg::Window("ScrollArea + TableView only", webApp);
    isolatedWindow->setInitialSize(540.0f, 420.0f);
    isolatedWindow->setInitialPosition(1040.0f, 28.0f);
    isolatedWindow->setResizable(true);

    auto* isolatedLayout = new wg::VBoxLayout(isolatedWindow);
    isolatedWindow->setLayout(isolatedLayout);

    auto* isolatedTitle = new wg::Label("Standalone TableView inside ScrollArea", isolatedWindow);
    isolatedLayout->addWidget(isolatedTitle);

    auto* isolatedScrollArea = new wg::ScrollArea(isolatedWindow);
    isolatedScrollArea->setWidgetResizable(true);
    isolatedScrollArea->setVerticalScrollBarPolicy(wg::ScrollArea::ScrollBarPolicy::AsNeeded);
    isolatedScrollArea->setHorizontalScrollBarPolicy(wg::ScrollArea::ScrollBarPolicy::AsNeeded);
    isolatedLayout->addWidget(isolatedScrollArea, 1);

    auto* isolatedTable = new wg::TableView(isolatedWindow);
    isolatedTable->setStretchLastColumn(true);
    isolatedTable->setModel(&rightModel);
    isolatedTable->setSelectionBehavior(wg::TableSelectionBehavior::Rows);
    isolatedScrollArea->setWidget(isolatedTable);

    webApp->run();
    delete webApp;

    return 0;
}
