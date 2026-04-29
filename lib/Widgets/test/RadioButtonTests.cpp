#include <gtest/gtest.h>
#include "SNFWidgets/RadioButton.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

class RadioButtonFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(RadioButtonFixture, defaultState)
{
    RadioButton rb("Option A");
    EXPECT_EQ(rb.label(), "Option A");
    EXPECT_FALSE(rb.isSelected());
}

TEST_F(RadioButtonFixture, setSelectedTrue)
{
    RadioButton rb("Option A");
    rb.setSelected(true);
    EXPECT_TRUE(rb.isSelected());
}

TEST_F(RadioButtonFixture, setSelectedFalse)
{
    RadioButton rb("Option A");
    rb.setSelected(true);
    rb.setSelected(false);
    EXPECT_FALSE(rb.isSelected());
}

TEST_F(RadioButtonFixture, setLabel)
{
    RadioButton rb("Old");
    rb.setLabel("New");
    EXPECT_EQ(rb.label(), "New");
}

TEST_F(RadioButtonFixture, linkIsBidirectional)
{
    RadioButton a("A");
    RadioButton b("B");

    a.link(&b);

    EXPECT_TRUE(a.isLinkedTo(&b));
    EXPECT_TRUE(b.isLinkedTo(&a));
    EXPECT_EQ(a.linkedButtonCount(), 1);
    EXPECT_EQ(b.linkedButtonCount(), 1);
}

TEST_F(RadioButtonFixture, linkMergesGroups)
{
    RadioButton a("A");
    RadioButton b("B");
    RadioButton c("C");

    a.link(&b);
    b.link(&c);

    EXPECT_TRUE(a.isLinkedTo(&b));
    EXPECT_TRUE(a.isLinkedTo(&c));
    EXPECT_TRUE(b.isLinkedTo(&a));
    EXPECT_TRUE(b.isLinkedTo(&c));
    EXPECT_TRUE(c.isLinkedTo(&a));
    EXPECT_TRUE(c.isLinkedTo(&b));
    EXPECT_EQ(a.linkedButtonCount(), 2);
    EXPECT_EQ(b.linkedButtonCount(), 2);
    EXPECT_EQ(c.linkedButtonCount(), 2);
}

TEST_F(RadioButtonFixture, linkAllCreatesOneExclusiveGroup)
{
    RadioButton a("A");
    RadioButton b("B");
    RadioButton c("C");

    RadioButton::linkAll({&a, &b, &c});

    EXPECT_TRUE(a.isLinkedTo(&b));
    EXPECT_TRUE(a.isLinkedTo(&c));
    EXPECT_TRUE(b.isLinkedTo(&c));
}

TEST_F(RadioButtonFixture, linkedButtonsAreExclusiveWhenSelectedProgrammatically)
{
    RadioButton a("A");
    RadioButton b("B");
    RadioButton c("C");
    RadioButton::linkAll({&a, &b, &c});

    a.setSelected(true);
    EXPECT_TRUE(a.isSelected());
    EXPECT_FALSE(b.isSelected());
    EXPECT_FALSE(c.isSelected());

    b.setSelected(true);
    EXPECT_FALSE(a.isSelected());
    EXPECT_TRUE(b.isSelected());
    EXPECT_FALSE(c.isSelected());

    c.setSelected(true);
    EXPECT_FALSE(a.isSelected());
    EXPECT_FALSE(b.isSelected());
    EXPECT_TRUE(c.isSelected());
}

TEST_F(RadioButtonFixture, linkingSelectedButtonsKeepsOnlyOneSelected)
{
    RadioButton a("A");
    RadioButton b("B");

    a.setSelected(true);
    b.setSelected(true);

    a.link(&b);

    EXPECT_TRUE(a.isSelected());
    EXPECT_FALSE(b.isSelected());
}

TEST_F(RadioButtonFixture, linkIgnoresNullAndSelf)
{
    RadioButton a("A");

    a.link(nullptr);
    a.link(&a);

    EXPECT_EQ(a.linkedButtonCount(), 0);
}

// Programmatic setter does NOT emit stateChanged.
TEST_F(RadioButtonFixture, setterDoesNotEmitSignal)
{
    RadioButton rb("x");
    int count = 0;
    rb.stateChanged.connect([&](bool) { ++count; });
    rb.setSelected(true);
    rb.setSelected(false);
    EXPECT_EQ(count, 0);
}

TEST_F(RadioButtonFixture, programmaticExclusiveChangeDoesNotEmitSignals)
{
    RadioButton a("A");
    RadioButton b("B");
    a.link(&b);

    int aCount = 0;
    int bCount = 0;
    a.stateChanged.connect([&](bool) { ++aCount; });
    b.stateChanged.connect([&](bool) { ++bCount; });

    a.setSelected(true);
    b.setSelected(true);

    EXPECT_FALSE(a.isSelected());
    EXPECT_TRUE(b.isSelected());
    EXPECT_EQ(aCount, 0);
    EXPECT_EQ(bCount, 0);
}
