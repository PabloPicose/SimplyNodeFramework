#include <SNFCore/Application.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/ScrollArea.h>
#include <SNFWidgets/Window.h>

#include <SNFWidgets/ImGuiDemoWindow.h>

namespace wg = snf::widgets;

int main()
{
    snf::Application app(0, nullptr);

    auto* appNode = new wg::ApplicationNode();
    appNode->setTitle("SNF VBox Stretch Scroll Example");

    new wg::ImGuiDemoWindow(appNode);

    // -----------------------------------------------------------------------
    // Main window
    // -----------------------------------------------------------------------
    auto* window = new wg::Window("VBox + trailing stretch", appNode);
    window->setInitialSize(460.0f, 360.0f);
    window->setInitialPosition(40.0f, 40.0f);
    window->setResizable(true);
    // Disable the window's own scrollbar; the ScrollArea below handles it.
    window->setScrollEnabled(false);

    auto* outerLayout = new wg::VBoxLayout(window);
    window->setLayout(outerLayout);

    // The ScrollArea fills the whole window and drives vertical scrolling.
    // With widgetResizable=true the inner VBoxLayout gets at least sizeHint()
    // height, so the trailing stretch expands freely when there is room, and
    // collapses to zero when the viewport shrinks — only after the real widgets
    // can no longer fit does the scrollbar appear.
    auto* scroll = new wg::ScrollArea(window);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(wg::ScrollArea::ScrollBarPolicy::AlwaysOff);
    outerLayout->addWidget(scroll, 1);

    // -----------------------------------------------------------------------
    // Inner VBoxLayout — the content scrolled by the ScrollArea
    // -----------------------------------------------------------------------
    auto* content = new wg::VBoxLayout(window);
    scroll->setWidget(content);

    // Row 1: Name
    auto* nameRow = new wg::HBoxLayout(window);
    content->addWidget(nameRow);

    auto* nameLabel = new wg::Label("Name:", window);
    nameRow->addWidget(nameLabel);

    auto* nameEdit = new wg::LineEdit("##name", window);
    nameEdit->setPlaceholder("Enter your name...");
    nameRow->addWidget(nameEdit, 1);

    // Row 2: Email
    auto* emailRow = new wg::HBoxLayout(window);
    content->addWidget(emailRow);

    auto* emailLabel = new wg::Label("Email:", window);
    emailRow->addWidget(emailLabel);

    auto* emailEdit = new wg::LineEdit("##email", window);
    emailEdit->setPlaceholder("Enter your email...");
    emailRow->addWidget(emailEdit, 1);

    // Row 3: Phone
    auto* phoneRow = new wg::HBoxLayout(window);
    content->addWidget(phoneRow);

    auto* phoneLabel = new wg::Label("Phone:", window);
    phoneRow->addWidget(phoneLabel);

    auto* phoneEdit = new wg::LineEdit("##phone", window);
    phoneEdit->setPlaceholder("Enter your phone number...");
    phoneRow->addWidget(phoneEdit, 1);

    // Row 4: Options
    auto* optRow = new wg::HBoxLayout(window);
    content->addWidget(optRow);

    auto* chkNewsletter = new wg::CheckBox("Newsletter", window);
    chkNewsletter->setChecked(true);
    optRow->addWidget(chkNewsletter);

    auto* chkTerms = new wg::CheckBox("Accept terms", window);
    optRow->addWidget(chkTerms);

    optRow->addStretch(1);

    // Row 5: Submit button (right-aligned)
    auto* submitRow = new wg::HBoxLayout(window);
    content->addWidget(submitRow);

    submitRow->addStretch(1);

    auto* submitBtn = new wg::PushButton("Submit", window);
    submitRow->addWidget(submitBtn);

    // -----------------------------------------------------------------------
    // Trailing vertical stretch
    //
    // This stretch absorbs all the free vertical space when the viewport is
    // larger than the widget rows.  When the viewport shrinks the stretch is
    // the FIRST thing to give up space (it has no minimum height).  Only once
    // the stretch has collapsed to zero and the real widgets can no longer fit
    // should the ScrollArea start showing a vertical scrollbar.
    //
    // Bug (before fix): VBoxLayout counted the StretchSpacer in its spacing
    // arithmetic, so sizeHint() was one ItemSpacing.y too tall.  The scrollbar
    // appeared one spacing unit too early, giving the impression that the
    // stretch "needed" that space.
    // -----------------------------------------------------------------------
    content->addStretch(1);

    appNode->run();
    return 0;
}
