#include <gtest/gtest.h>

#include "SNFWidgets/Label.h"
#include "SNFWidgets/Layout.h"
#include "SNFWidgets/LineEdit.h"
#include "SNFWidgets/PushButton.h"
#include "SNFWidgets/SpinBox.h"
#include "SNFWidgets/Window.h"
#include "ImGuiInteractionHarness.h"

#include "imgui.h"

#include <SNFCore/Application.h>

using namespace snf::widgets;

namespace {

class RecordingButton final : public PushButton
{
public:
    using PushButton::PushButton;

    const test::InteractionRect& lastRect() const { return m_lastRect; }

protected:
    void renderImGuiConstrained(float width, float height) override
    {
        PushButton::renderImGuiConstrained(width, height);
        m_lastRect = test::InteractionRect{
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::IsItemHovered(),
            ImGui::IsItemActive()};
    }

private:
    test::InteractionRect m_lastRect;
};

class RecordingLineEdit final : public LineEdit
{
public:
    using LineEdit::LineEdit;

    const test::InteractionRect& lastInputRect() const { return m_lastInputRect; }

protected:
    void renderImGuiConstrained(float width, float height) override
    {
        LineEdit::renderImGuiConstrained(width, height);
        m_lastInputRect = test::InteractionRect{
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::IsItemHovered(),
            ImGui::IsItemActive()};
    }

private:
    test::InteractionRect m_lastInputRect;
};

class RecordingSpinBox final : public SpinBox
{
public:
    using SpinBox::SpinBox;

    const test::InteractionRect& lastItemRect() const { return m_lastItemRect; }

protected:
    void renderImGuiConstrained(float width, float height) override
    {
        SpinBox::renderImGuiConstrained(width, height);
        m_lastItemRect = test::InteractionRect{
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::IsItemHovered(),
            ImGui::IsItemActive()};
    }

private:
    test::InteractionRect m_lastItemRect;
};

class ConstrainedHBoxLayout final : public HBoxLayout
{
public:
    void renderConstrainedForTest(float width, float height)
    {
        renderImGuiConstrained(width, height);
    }
};

void renderLayout(test::ImGuiInteractionHarness& harness, test::TestWidget<HBoxLayout>& layout)
{
    harness.beginFrame();
    layout.renderForTest();
    harness.endFrame();
}

}  // namespace

class LayoutFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }

    snf::Application* app = nullptr;
};

TEST_F(LayoutFixture, addWidgetReparentsAndStoresStretch)
{
    VBoxLayout layout;
    Label label("Title");

    layout.addWidget(&label, 2);

    EXPECT_EQ(layout.count(), 1);
    EXPECT_EQ(layout.widgetAt(0), &label);
    EXPECT_EQ(layout.stretchAt(0), 2);
    EXPECT_EQ(label.parent(), &layout);
}

TEST_F(LayoutFixture, addStretchAndAddSpacingCreateNonWidgetItems)
{
    HBoxLayout layout;

    layout.addStretch();
    layout.addSpacing(20.0f);

    EXPECT_EQ(layout.count(), 2);
    EXPECT_EQ(layout.itemTypeAt(0), Layout::LayoutItemType::StretchSpacer);
    EXPECT_EQ(layout.itemTypeAt(1), Layout::LayoutItemType::FixedSpacer);
    EXPECT_EQ(layout.widgetAt(0), nullptr);
    EXPECT_EQ(layout.widgetAt(1), nullptr);
    EXPECT_EQ(layout.stretchAt(0), 1);
    EXPECT_FLOAT_EQ(layout.fixedSpacingAt(1), 20.0f);
    EXPECT_EQ(layout.childrenCount(), 0U);
}

TEST_F(LayoutFixture, addWidgetKeepsExistingParent)
{
    Window window("Settings");
    HBoxLayout layout(&window);
    PushButton button("Apply", &window);

    layout.addWidget(&button, 1);

    EXPECT_EQ(layout.count(), 1);
    EXPECT_EQ(layout.widgetAt(0), &button);
    EXPECT_EQ(button.parent(), &window);
}

TEST_F(LayoutFixture, addWidgetUpdatesExistingEntry)
{
    HBoxLayout layout;
    PushButton button("Apply");

    layout.addWidget(&button, 1);
    layout.addWidget(&button, 3);

    EXPECT_EQ(layout.count(), 1);
    EXPECT_EQ(layout.widgetAt(0), &button);
    EXPECT_EQ(layout.stretchAt(0), 3);
}

TEST_F(LayoutFixture, horizontalStretchPushesButtonsToTheRight)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> layout;
    RecordingButton first("One");
    RecordingButton second("Two");

    layout.setSpacing(4.0f);
    layout.addStretch();
    layout.addWidget(&first);
    layout.addWidget(&second);

    renderLayout(harness, layout);

    EXPECT_GT(first.lastRect().min.x, 600.0f);
    EXPECT_GT(second.lastRect().min.x, first.lastRect().max.x);
    EXPECT_NEAR(second.lastRect().min.x - first.lastRect().max.x, 4.0f, 1.0f);
}

TEST_F(LayoutFixture, horizontalStretchBetweenButtonsSeparatesThem)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> layout;
    RecordingButton first("Left");
    RecordingButton second("Right");

    layout.setSpacing(4.0f);
    layout.addWidget(&first);
    layout.addStretch();
    layout.addWidget(&second);

    renderLayout(harness, layout);

    EXPECT_LT(first.lastRect().min.x, 80.0f);
    EXPECT_GT(second.lastRect().min.x, 650.0f);
}

TEST_F(LayoutFixture, twoHorizontalStretchesCenterWidget)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> layout;
    RecordingButton button("Center");

    layout.setSpacing(0.0f);
    layout.addStretch();
    layout.addWidget(&button);
    layout.addStretch();

    renderLayout(harness, layout);

    const float center = (button.lastRect().min.x + button.lastRect().max.x) * 0.5f;
    EXPECT_NEAR(center, 412.0f, 8.0f);
}

TEST_F(LayoutFixture, stretchFactorsDistributeRemainingWidthProportionally)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> layout;
    RecordingButton first("One");
    RecordingButton second("Two");

    layout.setSpacing(0.0f);
    layout.addStretch(1);
    layout.addWidget(&first);
    layout.addStretch(2);
    layout.addWidget(&second);

    renderLayout(harness, layout);

    const float leftStretch = first.lastRect().min.x - 32.0f;
    const float middleStretch = second.lastRect().min.x - first.lastRect().max.x;
    EXPECT_NEAR(middleStretch, leftStretch * 2.0f, 8.0f);
}

TEST_F(LayoutFixture, fixedSpacingAddsPixelsBetweenWidgets)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> layout;
    RecordingButton first("One");
    RecordingButton second("Two");

    layout.setSpacing(0.0f);
    layout.addWidget(&first);
    layout.addSpacing(20.0f);
    layout.addWidget(&second);

    renderLayout(harness, layout);

    EXPECT_NEAR(second.lastRect().min.x - first.lastRect().max.x, 20.0f, 1.0f);
}

TEST_F(LayoutFixture, horizontalLayoutKeepsLineEditMinimumBeforeStretchedItems)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> layout;
    RecordingLineEdit host("Host");
    RecordingSpinBox port("Port", 3000, 32000);
    RecordingButton connect("Connect");

    host.setText("127.0.0.1");
    port.setValue(30123);
    layout.setSpacing(4.0f);
    layout.addWidget(&host);
    layout.addWidget(&port, 1);
    layout.addWidget(&connect);

    renderLayout(harness, layout);

    const float hostWidth = host.lastInputRect().max.x - host.lastInputRect().min.x;
    EXPECT_GE(hostWidth, 120.0f);
    EXPECT_GT(port.lastItemRect().min.x, host.lastInputRect().max.x);
    EXPECT_GT(connect.lastRect().min.x, port.lastItemRect().max.x);
}

TEST_F(LayoutFixture, stretchedLineEditStillStartsFromItsMinimumWidth)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> layout;
    RecordingLineEdit host;
    RecordingButton connect("Connect");

    host.setText("127.0.0.1");
    layout.setSpacing(4.0f);
    layout.addWidget(&host, 1);
    layout.addWidget(&connect);

    renderLayout(harness, layout);

    const float hostWidth = host.lastInputRect().max.x - host.lastInputRect().min.x;
    EXPECT_GE(hostWidth, 128.0f);
    EXPECT_GT(connect.lastRect().min.x, host.lastInputRect().max.x);
}

TEST_F(LayoutFixture, nestedVerticalLayoutProvidesNaturalWidthToHorizontalLayout)
{
    test::ImGuiInteractionHarness harness;
    test::TestWidget<HBoxLayout> root;
    VBoxLayout sideBar;
    RecordingButton newTab("New tab");
    RecordingButton removeCurrent("Remove current");

    sideBar.addWidget(&newTab);
    sideBar.addWidget(&removeCurrent);
    root.addStretch();
    root.addWidget(&sideBar);

    harness.beginFrame();
    const Size sideHint = sideBar.sizeHint();
    const Size rootHint = root.sizeHint();
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    harness.endFrame();

    EXPECT_GE(sideHint.width, removeCurrent.sizeHint().width);
    EXPECT_GE(rootHint.width, sideHint.width);

    renderLayout(harness, root);

    EXPECT_GT(newTab.lastRect().min.x, 650.0f);
    EXPECT_NEAR(removeCurrent.lastRect().min.x, newTab.lastRect().min.x, 1.0f);
    EXPECT_GT(removeCurrent.lastRect().min.y, newTab.lastRect().max.y);
}

TEST_F(LayoutFixture, constrainedHorizontalLayoutCanRenderNarrowerThanItsContents)
{
    test::ImGuiInteractionHarness harness;
    ConstrainedHBoxLayout layout;
    RecordingButton first("First");
    RecordingButton second("Second");

    layout.setSpacing(4.0f);
    layout.addWidget(&first);
    layout.addWidget(&second);

    harness.beginFrame();
    layout.renderConstrainedForTest(20.0f, 40.0f);
    harness.endFrame();

    EXPECT_GT(first.lastRect().max.x, first.lastRect().min.x);
    EXPECT_GT(second.lastRect().max.x, second.lastRect().min.x);
}

TEST_F(LayoutFixture, containsWidgetFindsNestedLayoutItems)
{
    VBoxLayout mainLayout;
    HBoxLayout hbox;
    PushButton button("Apply");

    mainLayout.addWidget(&hbox);
    hbox.addWidget(&button);

    EXPECT_TRUE(mainLayout.containsWidget(&hbox));
    EXPECT_TRUE(mainLayout.containsWidget(&button));
    EXPECT_TRUE(hbox.containsWidget(&button));
}

TEST_F(LayoutFixture, removeWidgetKeepsWidgetAlive)
{
    VBoxLayout layout;
    Label label("Status");

    layout.addWidget(&label);
    layout.removeWidget(&label);

    EXPECT_EQ(layout.count(), 0);
    EXPECT_EQ(label.parent(), &layout);
}

TEST_F(LayoutFixture, windowSetLayoutReparentsLayout)
{
    Window window("Settings");
    VBoxLayout layout;

    window.setLayout(&layout);

    EXPECT_EQ(window.layout(), &layout);
    EXPECT_EQ(layout.parent(), &window);
}

TEST_F(LayoutFixture, formLayoutStoresRows)
{
    FormLayout form;
    Label host("127.0.0.1");
    PushButton apply("Apply");

    form.addRow("Host", &host, 2);
    form.addRow("", &apply, 1);

    EXPECT_EQ(form.rowCount(), 2);
    EXPECT_EQ(form.rowLabelAt(0), "Host");
    EXPECT_EQ(form.rowWidgetAt(0), &host);
    EXPECT_EQ(form.rowStretchAt(0), 2);
    EXPECT_EQ(form.rowLabelAt(1), "");
    EXPECT_EQ(form.rowWidgetAt(1), &apply);
    EXPECT_EQ(host.parent(), &form);
    EXPECT_EQ(apply.parent(), &form);
}
