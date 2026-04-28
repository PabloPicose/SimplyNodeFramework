#include <gtest/gtest.h>

#include "SNFWidgets/Label.h"
#include "SNFWidgets/Layout.h"
#include "SNFWidgets/PushButton.h"
#include "SNFWidgets/Window.h"

#include <SNFCore/Application.h>

using namespace snf::widgets;

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
