#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/Slider.h>
#include <SNFWidgets/StackedWidget.h>
#include <SNFWidgets/TextEdit.h>
#include <SNFWidgets/Window.h>

namespace wg = snf::widgets;

// ─── Page factories ───────────────────────────────────────────────────────────

static wg::Widget* createWelcomePage(wg::Window* win)
{
    auto* layout = new wg::VBoxLayout(win);

    auto* heading = new wg::Label("Welcome to the StackedWidget example", layout);
    layout->addWidget(heading);
    layout->addSpacing(12.0f);

    auto* body = new wg::Label(
        "This window uses a StackedWidget to switch between three panels\n"
        "without showing a tab bar.  Use the navigation buttons on the right\n"
        "to move between pages.",
        layout);
    layout->addWidget(body);
    layout->addSpacing(16.0f);

    auto* visibilityNote = new wg::Label("The checkbox below toggles the heading above.", layout);
    layout->addWidget(visibilityNote);

    auto* toggleHeading = new wg::CheckBox("Show heading", layout);
    toggleHeading->setChecked(true);
    toggleHeading->stateChanged.connect([heading](bool checked) {
        heading->setVisible(checked);
    });
    layout->addWidget(toggleHeading);

    return layout;
}

static wg::Widget* createSettingsPage(wg::Window* win)
{
    auto* layout = new wg::VBoxLayout(win);

    auto* heading = new wg::Label("Settings", layout);
    layout->addWidget(heading);
    layout->addSpacing(8.0f);

    auto* nameEdit = new wg::LineEdit(layout);
    nameEdit->setPlaceholder("Username");
    layout->addWidget(nameEdit);
    layout->addSpacing(4.0f);

    auto* serverEdit = new wg::LineEdit(layout);
    serverEdit->setPlaceholder("Server address");
    serverEdit->setText("192.168.1.1");
    layout->addWidget(serverEdit);
    layout->addSpacing(8.0f);

    auto* notifCheck = new wg::CheckBox("Enable notifications", layout);
    notifCheck->setChecked(true);
    layout->addWidget(notifCheck);

    auto* autoCheck = new wg::CheckBox("Auto-connect on startup", layout);
    layout->addWidget(autoCheck);
    layout->addStretch();

    auto* saveBtn = new wg::PushButton("Save settings", layout);
    layout->addWidget(saveBtn);

    return layout;
}

static wg::Widget* createProgressPage(wg::Window* win)
{
    auto* layout = new wg::VBoxLayout(win);

    auto* heading = new wg::Label("Progress demo", layout);
    layout->addWidget(heading);
    layout->addSpacing(8.0f);

    auto* slider = new wg::Slider("Value", 0, 100, layout);
    slider->setValue(42);
    layout->addWidget(slider);
    layout->addSpacing(6.0f);

    auto* bar = new wg::ProgressBar(0, 100, layout);
    bar->setValue(42);
    layout->addWidget(bar);
    layout->addSpacing(8.0f);

    slider->valueChanged.connect([bar](int value) {
        bar->setValue(value);
    });

    auto* log = new wg::TextEdit(layout);
    log->setText("Drag the slider to update the progress bar.\n");
    log->setReadOnly(true);
    layout->addWidget(log, 1);

    slider->valueChanged.connect([log](int value) {
        log->setText(log->text() + "Value changed to " + std::to_string(value) + "\n");
    });

    return layout;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    snf::Application app(argc, argv);
    auto* appNode = new wg::ApplicationNode();
    appNode->setTitle("SNF StackedWidget Example");

    auto* window = new wg::Window("StackedWidget demo", appNode);
    window->setInitialSize(800.0f, 520.0f);
    window->setInitialPosition(40.0f, 40.0f);
    window->setResizable(true);

    // Root layout: stacked content on the left, nav buttons on the right
    auto* root = new wg::HBoxLayout(window);
    root->setSpacing(8.0f);
    window->setLayout(root);

    // ── StackedWidget ──────────────────────────────────────────────────────
    auto* stack = new wg::StackedWidget(window);

    wg::Widget* pages[] = {
        createWelcomePage(window),
        createSettingsPage(window),
        createProgressPage(window),
    };

    stack->addWidget(pages[0]);
    stack->addWidget(pages[1]);
    stack->addWidget(pages[2]);

    root->addWidget(stack, 1);

    // ── Navigation panel ──────────────────────────────────────────────────
    auto* nav = new wg::VBoxLayout(window);
    nav->setSpacing(4.0f);
    root->addWidget(nav);

    const char* navLabels[] = {"Welcome", "Settings", "Progress"};
    wg::PushButton* navBtns[3] = {};

    for (int i = 0; i < 3; ++i) {
        navBtns[i] = new wg::PushButton(navLabels[i], window);
        nav->addWidget(navBtns[i]);
    }

    nav->addStretch();

    auto* pageLabel = new wg::Label("Page 1 / 3", window);
    nav->addWidget(pageLabel);

    nav->addSpacing(4.0f);

    auto* prevBtn = new wg::PushButton("< Prev", window);
    auto* nextBtn = new wg::PushButton("Next >", window);
    nav->addWidget(prevBtn);
    nav->addWidget(nextBtn);

    // ── Connections ───────────────────────────────────────────────────────
    auto updateLabel = [pageLabel, stack]() {
        const int idx = stack->currentIndex();
        const int total = stack->count();
        pageLabel->setText("Page " + std::to_string(idx + 1)
                           + " / " + std::to_string(total));
    };

    for (int i = 0; i < 3; ++i) {
        navBtns[i]->clicked.connect([stack, i, updateLabel]() {
            stack->setCurrentIndex(i);
            updateLabel();
        });
    }

    prevBtn->clicked.connect([stack, updateLabel]() {
        const int idx = stack->currentIndex();
        if (idx > 0) {
            stack->setCurrentIndex(idx - 1);
            updateLabel();
        }
    });

    nextBtn->clicked.connect([stack, updateLabel]() {
        const int idx = stack->currentIndex();
        if (idx < stack->count() - 1) {
            stack->setCurrentIndex(idx + 1);
            updateLabel();
        }
    });

    appNode->run();
    delete appNode;

    return 0;
}
