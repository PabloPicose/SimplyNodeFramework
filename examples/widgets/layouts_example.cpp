#include <SNFCore/Application.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/RadioButton.h>
#include <SNFWidgets/TextEdit.h>
#include <SNFWidgets/Window.h>
#include <SNFWidgets/ImGuiDemoWindow.h>


#include <string>

namespace wg = snf::widgets;

int main()
{
    snf::Application app(0, nullptr);

    auto* appNode = new wg::ApplicationNode();
    appNode->setTitle("SNF Layouts Example");

    // IMGUI DEMO
    auto* demoWindow = new wg::ImGuiDemoWindow(appNode);

    // -----------------------------------------------------------------------
    // Main window
    // -----------------------------------------------------------------------
    auto* window = new wg::Window("Layouts demo", appNode);
    window->setInitialSize(520.0f, 400.0f);
    window->setInitialPosition(40.0f, 40.0f);
    window->setResizable(true);

    auto* root = new wg::VBoxLayout(window);
    window->setLayout(root);

    // -----------------------------------------------------------------------
    // Row 1: label + line edit (name entry)
    // -----------------------------------------------------------------------
    auto* nameRow = new wg::HBoxLayout(window);
    root->addWidget(nameRow);

    auto* nameLabel = new wg::Label("Name:", window);
    nameRow->addWidget(nameLabel);

    auto* nameEdit = new wg::LineEdit("##name", window);
    nameEdit->setPlaceholder("Enter your name…");
    nameRow->addWidget(nameEdit, 1);

    // -----------------------------------------------------------------------
    // Row 2: label + line edit (email entry)
    // -----------------------------------------------------------------------
    auto* emailRow = new wg::HBoxLayout(window);
    root->addWidget(emailRow);

    auto* emailLabel = new wg::Label("Email:", window);
    emailRow->addWidget(emailLabel);

    auto* emailEdit = new wg::LineEdit("##email", window);
    emailEdit->setPlaceholder("Enter your email…");
    emailRow->addWidget(emailEdit, 1);

    // -----------------------------------------------------------------------
    // Row 3: checkboxes
    // -----------------------------------------------------------------------
    auto* checkRow = new wg::HBoxLayout(window);
    root->addWidget(checkRow);

    auto* chkNewsletter = new wg::CheckBox("Subscribe to newsletter", window);
    chkNewsletter->setChecked(true);
    checkRow->addWidget(chkNewsletter);

    checkRow->addStretch(1);

    auto* chkTerms = new wg::CheckBox("Accept terms", window);
    checkRow->addWidget(chkTerms);

    // -----------------------------------------------------------------------
    // Row 4: radio buttons (role selection)
    // -----------------------------------------------------------------------
    auto* roleRow = new wg::HBoxLayout(window);
    root->addWidget(roleRow);

    auto* roleLabel = new wg::Label("Role:", window);
    roleRow->addWidget(roleLabel);

    auto* radioUser  = new wg::RadioButton("User",  window);
    auto* radioAdmin = new wg::RadioButton("Admin", window);
    auto* radioGuest = new wg::RadioButton("Guest", window);
    radioUser->link(radioAdmin);
    radioAdmin->link(radioGuest);
    radioUser->setSelected(true);

    roleRow->addWidget(radioUser);
    roleRow->addWidget(radioAdmin);
    roleRow->addWidget(radioGuest);
    roleRow->addStretch(1);

    // -----------------------------------------------------------------------
    // Row 5: label + checkbox alignment demo
    // -----------------------------------------------------------------------
    auto* alignRow = new wg::HBoxLayout(window);
    root->addWidget(alignRow);

    auto* alignLabel = new wg::Label("Save on disk", window);
    alignRow->addWidget(alignLabel);

    auto* chkSave = new wg::CheckBox(window);
    chkSave->setChecked(true);
    alignRow->addWidget(chkSave);

    auto* historyLabel = new wg::Label("Max history", window);
    alignRow->addWidget(historyLabel);

    auto* historyEdit = new wg::LineEdit("##history", window);
    historyEdit->setText("200");
    alignRow->addWidget(historyEdit, 1);

    // -----------------------------------------------------------------------
    // Row 6: progress bar with label
    // -----------------------------------------------------------------------
    auto* progressRow = new wg::HBoxLayout(window);
    root->addWidget(progressRow);

    auto* progressLabel = new wg::Label("Progress:", window);
    progressRow->addWidget(progressLabel);

    auto* progressBar = new wg::ProgressBar(0, 100, window);
    progressBar->setValue(0);
    progressBar->setOverlayText("0 %");
    progressRow->addWidget(progressBar, 1);

    // -----------------------------------------------------------------------
    // Row 6: action buttons
    // -----------------------------------------------------------------------
    auto* buttonRow = new wg::HBoxLayout(window);
    root->addWidget(buttonRow);

    buttonRow->addStretch(1);

    auto* btnReset  = new wg::PushButton("Reset",  window);
    auto* btnSubmit = new wg::PushButton("Submit", window);
    buttonRow->addWidget(btnReset);
    buttonRow->addWidget(btnSubmit);

    // -----------------------------------------------------------------------
    // Row 7: two side-by-side text editors
    // -----------------------------------------------------------------------
    auto* twoEditRow = new wg::HBoxLayout(window);
    root->addWidget(twoEditRow, 1);

    auto* leftEdit = new wg::TextEdit("Left editor", window);
    leftEdit->setText("Left side…");
    twoEditRow->addWidget(leftEdit, 1);

    auto* rightEdit = new wg::TextEdit("Right editor", window);
    rightEdit->setText("Right side…");
    twoEditRow->addWidget(rightEdit, 1);

    // -----------------------------------------------------------------------
    // Row 8: multi-line text editor (notes)
    // -----------------------------------------------------------------------
    auto* notesEdit = new wg::TextEdit(window);
    notesEdit->setText("Notes go here…");
    root->addWidget(notesEdit, 1);

    // -----------------------------------------------------------------------
    // Row 9: status label (feedback)
    // -----------------------------------------------------------------------
    auto* statusRow = new wg::HBoxLayout(window);
    root->addWidget(statusRow);

    auto* statusLabel = new wg::Label("Ready.", window);
    statusRow->addWidget(statusLabel);
    statusRow->addStretch(1);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    static int submitCount = 0;

    btnSubmit->clicked.connect([=]() mutable {
        ++submitCount;
        const int progress = std::min(submitCount * 20, 100);
        progressBar->setValue(progress);
        progressBar->setOverlayText(std::to_string(progress) + " %");
        statusLabel->setText("Submitted " + std::to_string(submitCount) + " time(s). Name: " + nameEdit->text());
    });

    btnReset->clicked.connect([=]() mutable {
        submitCount = 0;
        nameEdit->setText("");
        emailEdit->setText("");
        chkNewsletter->setChecked(true);
        chkTerms->setChecked(false);
        radioUser->setSelected(true);
        progressBar->setValue(0);
        progressBar->setOverlayText("0 %");
        statusLabel->setText("Ready.");
    });

    appNode->run();
    delete appNode;
    return 0;
}
