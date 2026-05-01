#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Application.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/ComboBox.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/Slider.h>
#include <SNFWidgets/SpinBox.h>
#include <SNFWidgets/Splitter.h>
#include <SNFWidgets/TableView.h>
#include <SNFWidgets/Tabs.h>
#include <SNFWidgets/TextEdit.h>
#include <SNFWidgets/Window.h>

#include <string>
#include <utility>
#include <vector>

namespace wg = snf::widgets;

class DeviceModel final : public snf::AbstractTableModel
{
public:
    DeviceModel()
        : m_rows{
              {"Node A", "Online",  "23 ms",  "Stable"},
              {"Node B", "Offline", "-",      "Retrying"},
              {"Node C", "Online",  "41 ms",  "Stable"},
              {"Node D", "Online",  "17 ms",  "Nominal"},
              {"Node E", "Warning", "88 ms",  "Packet loss"},
          }
    {
    }

    int rowCount() const override
    {
        return static_cast<int>(m_rows.size());
    }

    int columnCount() const override
    {
        return 4;
    }

    std::string data(int row, int column) const override
    {
        if (row < 0 || row >= rowCount() || column < 0 || column >= columnCount()) {
            return {};
        }
        return m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    }

    std::string headerData(int section) const override
    {
        switch (section) {
        case 0: return "Device";
        case 1: return "State";
        case 2: return "Latency";
        case 3: return "Details";
        default: return {};
        }
    }

private:
    std::vector<std::vector<std::string>> m_rows;
};

static wg::Widget* createDynamicTabPage(int number)
{
    auto* page = new wg::VBoxLayout();

    auto* title = new wg::Label("Dynamic tab " + std::to_string(number), page);
    page->addWidget(title);
    page->addSpacing(8.0f);

    auto* nameEdit = new wg::LineEdit("Name", page);
    nameEdit->setPlaceholder("Editable field");
    page->addWidget(nameEdit);
    page->addSpacing(6.0f);

    auto* notes = new wg::TextEdit(page);
    notes->setText(
        "This tab was created at runtime.\n"
        "You can remove it with the external button.");
    page->addWidget(notes, 1);

    return page;
}

int main()
{
    snf::Application app(0, nullptr);
    DeviceModel deviceModel;

    auto* webApp = new wg::ApplicationNode();
    webApp->setTitle("SNF Tabs Example");

    auto* window = new wg::Window("Tabs demo", webApp);
    window->setInitialSize(1180.0f, 720.0f);
    window->setInitialPosition(28.0f, 28.0f);
    window->setResizable(true);

    auto* mainLayout = new wg::HBoxLayout(window);
    mainLayout->setSpacing(8.0f);
    window->setLayout(mainLayout);

    auto* tabs = new wg::Tabs(window);
    mainLayout->addWidget(tabs, 1);

    auto* actions = new wg::VBoxLayout(window);
    actions->setSpacing(4.0f);
    mainLayout->addWidget(actions);

    auto* addTabButton = new wg::PushButton("New tab", window);
    auto* removeTabButton = new wg::PushButton("Remove current", window);
    actions->addWidget(addTabButton);
    actions->addWidget(removeTabButton);
    actions->addStretch();

    // ---------------------------------------------------------------------
    // Tab 1: small collection of widgets
    // ---------------------------------------------------------------------
    auto* controlsTab = new wg::VBoxLayout();
    auto* controlsTitle = new wg::Label("General controls", controlsTab);
    controlsTab->addWidget(controlsTitle);
    controlsTab->addSpacing(8.0f);

    auto* controlsForm = new wg::FormLayout(controlsTab);
    auto* hostEdit = new wg::LineEdit(controlsTab);
    hostEdit->setText("127.0.0.1");
    controlsForm->addRow("Host", hostEdit);

    auto* portSpin = new wg::SpinBox("Port", 1, 65535, controlsTab);
    portSpin->setValue(9000);
    controlsForm->addRow("Port", portSpin);
    controlsTab->addWidget(controlsForm);
    controlsTab->addSpacing(8.0f);

    auto* autoConnect = new wg::CheckBox("Auto connect", controlsTab);
    autoConnect->setChecked(true);
    controlsTab->addWidget(autoConnect);

    auto* transportBox = new wg::ComboBox("Transport", controlsTab);
    transportBox->addItems({"TCP", "UDP", "WebSocket"});
    transportBox->setCurrentIndex(0);
    controlsTab->addWidget(transportBox);
    controlsTab->addSpacing(8.0f);

    auto* loadSlider = new wg::Slider("Load", 0, 100, controlsTab);
    loadSlider->setValue(42);
    controlsTab->addWidget(loadSlider);

    auto* loadProgress = new wg::ProgressBar(0, 100, controlsTab);
    loadProgress->setValue(42);
    loadProgress->setOverlayText("42 %");
    controlsTab->addWidget(loadProgress);
    controlsTab->addStretch();

    auto* connectButton = new wg::PushButton("Connect", controlsTab);
    controlsTab->addWidget(connectButton);

    tabs->addTab("Overview", controlsTab);

    // ---------------------------------------------------------------------
    // Tab 2: nested splitter with layouts and table view
    // ---------------------------------------------------------------------
    auto* splitTab = new wg::Splitter(wg::Splitter::Orientation::Horizontal);
    splitTab->setInitialRatio(0.42f);
    splitTab->setMinimumSizes(220.0f, 320.0f);

    auto* leftPane = new wg::VBoxLayout();
    auto* leftTitle = new wg::Label("Inspector", leftPane);
    leftPane->addWidget(leftTitle);
    leftPane->addSpacing(6.0f);

    auto* inspectorText = new wg::TextEdit(leftPane);
    inspectorText->setText(
        "Selected node: Node C\n"
        "Status: Online\n"
        "Last sync: 11:42:08\n"
        "Notes:\n"
        "- Replication healthy\n"
        "- Backups fresh\n");
    leftPane->addWidget(inspectorText, 1);

    auto* leftToggle = new wg::CheckBox("Lock selection", leftPane);
    leftPane->addWidget(leftToggle);
    splitTab->setPrimaryWidget(leftPane);

    auto* rightPane = new wg::VBoxLayout();
    auto* rightTitle = new wg::Label("Fleet", rightPane);
    rightPane->addWidget(rightTitle);
    rightPane->addSpacing(6.0f);

    auto* tableView = new wg::TableView(rightPane);
    tableView->setModel(&deviceModel);
    tableView->setSelectionBehavior(wg::TableSelectionBehavior::Rows);
    tableView->setSelectionMode(wg::TableSelectionMode::Single);
    tableView->setStretchLastColumn(true);
    rightPane->addWidget(tableView, 1);

    auto* refreshButton = new wg::PushButton("Refresh", rightPane);
    rightPane->addWidget(refreshButton);
    splitTab->setSecondaryWidget(rightPane);

    tabs->addTab("Workspace", splitTab);

    // ---------------------------------------------------------------------
    // Tab 3: notes / console style page
    // ---------------------------------------------------------------------
    auto* notesTab = new wg::VBoxLayout();
    auto* notesTitle = new wg::Label("Notes", notesTab);
    notesTab->addWidget(notesTitle);
    notesTab->addSpacing(8.0f);

    auto* notesEdit = new wg::TextEdit(notesTab);
    notesEdit->setText(
        "Deployment checklist:\n"
        "- Validate certificates\n"
        "- Confirm route propagation\n"
        "- Notify operations team\n");
    notesTab->addWidget(notesEdit, 1);

    auto* saveNotes = new wg::PushButton("Save notes", notesTab);
    notesTab->addWidget(saveNotes);

    tabs->addTab("Notes", notesTab);

    int nextDynamicTabNumber = 4;

    addTabButton->clicked.connect([&]() {
        const int index = tabs->addTab("Extra " + std::to_string(nextDynamicTabNumber),
                                       createDynamicTabPage(nextDynamicTabNumber));
        if (index >= 0) {
            tabs->setCurrentIndex(index);
            ++nextDynamicTabNumber;
        }
    });

    removeTabButton->clicked.connect([&]() {
        if (tabs->count() <= 0) {
            return;
        }
        tabs->removeTab(tabs->currentIndex());
    });

    webApp->run();
    delete webApp;

    return 0;
}
