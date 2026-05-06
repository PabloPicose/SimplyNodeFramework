#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Application.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/Slider.h>
#include <SNFWidgets/SpinBox.h>
#include <SNFWidgets/Splitter.h>
#include <SNFWidgets/TableView.h>
#include <SNFWidgets/TextEdit.h>
#include <SNFWidgets/Window.h>
#include <SNFWidgets/CheckBox.h>

#include <string>
#include <vector>

namespace wg = snf::widgets;

// ---------------------------------------------------------------------------
// Simple table model for panel B
// ---------------------------------------------------------------------------

class SensorModel final : public snf::AbstractTableModel
{
public:
    SensorModel()
        : m_rows{
              {"Temperature", "22.5 C", "OK"},
              {"Humidity",    "58 %",   "OK"},
              {"Pressure",    "1013 hPa","OK"},
              {"CO2",         "412 ppm","OK"},
              {"Light",       "320 lux","Low"},
          }
    {
    }

    int rowCount() const override
    {
        return static_cast<int>(m_rows.size());
    }

    int columnCount() const override
    {
        return 3;
    }

    std::string data(int row, int column) const override
    {
        if (! isValid(row, column)) {
            return {};
        }
        return m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    }

    std::string headerData(int section) const override
    {
        switch (section) {
        case 0: return "Sensor";
        case 1: return "Value";
        case 2: return "Status";
        default: return {};
        }
    }

private:
    bool isValid(int row, int column) const
    {
        return row >= 0
            && row < static_cast<int>(m_rows.size())
            && column >= 0
            && column < 3;
    }

    std::vector<std::vector<std::string>> m_rows;
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    snf::Application app(0, nullptr);
    SensorModel sensorModel;

    wg::ApplicationNode webApp;
    webApp.setTitle("SNF Splitter Example");

    // -----------------------------------------------------------------------
    // Main window
    // -----------------------------------------------------------------------
    auto* window = new wg::Window("Splitter layout demo", &webApp);
    window->setInitialSize(1000.0f, 620.0f);
    window->setInitialPosition(24.0f, 24.0f);
    window->setResizable(true);

    auto* mainLayout = new wg::VBoxLayout(window);
    window->setLayout(mainLayout);

    // -----------------------------------------------------------------------
    // Root: horizontal splitter  (left | right)
    // -----------------------------------------------------------------------
    auto* hSplitter = new wg::Splitter(wg::Splitter::Orientation::Horizontal, window);
    hSplitter->setInitialRatio(0.5f);
    mainLayout->addWidget(hSplitter, 1);

    // -----------------------------------------------------------------------
    // Left vertical splitter  (A / C)
    // -----------------------------------------------------------------------
    auto* leftSplitter = new wg::Splitter(wg::Splitter::Orientation::Vertical, window);
    leftSplitter->setInitialRatio(0.5f);
    hSplitter->setPrimaryWidget(leftSplitter);

    // --- Panel A: settings / controls ---
    auto* panelA = new wg::VBoxLayout(window);

    auto* labelA = new wg::Label("Panel A - Settings", window);
    panelA->addWidget(labelA);
    panelA->addSpacing(8.0f);

    auto* formA = new wg::FormLayout(window);
    auto* nameEdit = new wg::LineEdit(window);
    nameEdit->setPlaceholder("Enter your name...");
    formA->addRow("Name", nameEdit);

    auto* spinBox = new wg::SpinBox("Items", 1, 50, window);
    spinBox->setValue(10);
    formA->addRow("Count", spinBox);
    panelA->addWidget(formA);
    panelA->addSpacing(8.0f);

    auto* checkA = new wg::CheckBox("Enable notifications", window);
    checkA->setChecked(true);
    panelA->addWidget(checkA);

    auto* checkB = new wg::CheckBox("Dark mode", window);
    panelA->addWidget(checkB);
    panelA->addStretch();

    auto* applyBtn = new wg::PushButton("Apply", window);
    panelA->addWidget(applyBtn);

    leftSplitter->setPrimaryWidget(panelA);

    // --- Panel C: log / text output ---
    auto* panelC = new wg::VBoxLayout(window);

    auto* labelC = new wg::Label("Panel C - Log output", window);
    panelC->addWidget(labelC);

    auto* logEdit = new wg::TextEdit(window);
    logEdit->setText(
        "[INFO]  Application started.\n"
        "[INFO]  Splitter layout loaded.\n"
        "[WARN]  Light sensor reading is low.\n"
        "[INFO]  All other sensors nominal.\n");
    panelC->addWidget(logEdit, 1);

    auto* clearLogBtn = new wg::PushButton("Clear log", window);
    panelC->addWidget(clearLogBtn);

    clearLogBtn->clicked.connect([&]() {
        logEdit->setText("");
    });

    leftSplitter->setSecondaryWidget(panelC);

    // -----------------------------------------------------------------------
    // Right vertical splitter  (B / D)
    // -----------------------------------------------------------------------
    auto* rightSplitter = new wg::Splitter(wg::Splitter::Orientation::Vertical, window);
    rightSplitter->setInitialRatio(0.55f);
    hSplitter->setSecondaryWidget(rightSplitter);

    // --- Panel B: TableView ---
    auto* panelB = new wg::VBoxLayout(window);

    auto* labelB = new wg::Label("Panel B - Sensor readings", window);
    panelB->addWidget(labelB);

    auto* tableView = new wg::TableView(window);
    tableView->setModel(&sensorModel);
    tableView->setSelectionBehavior(wg::TableSelectionBehavior::Rows);
    tableView->setSelectionMode(wg::TableSelectionMode::Single);
    panelB->addWidget(tableView, 1);

    rightSplitter->setPrimaryWidget(panelB);

    // --- Panel D: sliders / progress bars ---
    auto* panelD = new wg::VBoxLayout(window);

    auto* labelD = new wg::Label("Panel D - Live values", window);
    panelD->addWidget(labelD);
    panelD->addSpacing(6.0f);

    auto* sliderTemp = new wg::Slider("Temperature", 0, 100, window);
    sliderTemp->setValue(23);
    panelD->addWidget(sliderTemp);

    auto* progressTemp = new wg::ProgressBar(0, 100, window);
    progressTemp->setValue(23);
    progressTemp->setOverlayText("23 %");
    panelD->addWidget(progressTemp);
    panelD->addSpacing(6.0f);

    auto* sliderHumidity = new wg::Slider("Humidity", 0, 100, window);
    sliderHumidity->setValue(58);
    panelD->addWidget(sliderHumidity);

    auto* progressHumidity = new wg::ProgressBar(0, 100, window);
    progressHumidity->setValue(58);
    progressHumidity->setOverlayText("58 %");
    panelD->addWidget(progressHumidity);
    panelD->addStretch();

    rightSplitter->setSecondaryWidget(panelD);

    // Keep progress bars in sync with sliders
    sliderTemp->valueChanged.connect([&](int v) {
        progressTemp->setValue(v);
        progressTemp->setOverlayText(std::to_string(v) + " %");
    });

    sliderHumidity->valueChanged.connect([&](int v) {
        progressHumidity->setValue(v);
        progressHumidity->setOverlayText(std::to_string(v) + " %");
    });

    // -----------------------------------------------------------------------
    // Run
    // -----------------------------------------------------------------------
    return app.run();
}
