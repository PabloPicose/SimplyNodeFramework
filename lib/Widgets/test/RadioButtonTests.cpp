#include <gtest/gtest.h>
#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/RadioButton.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

namespace {

using TestRadioButton = test::TestWidget<RadioButton>;

struct RadioPairRects {
    test::InteractionRect first;
    test::InteractionRect second;
};

test::InteractionRect renderRadio(test::ImGuiInteractionHarness& harness,
                                  TestRadioButton& radio,
                                  const ImVec2& mousePos = test::ImGuiInteractionHarness::offscreenMousePos(),
                                  bool mouseDown = false)
{
    harness.beginFrame(mousePos, mouseDown);
    const test::InteractionRect rect = harness.render(radio);
    harness.endFrame();
    return rect;
}

RadioPairRects renderRadioPair(test::ImGuiInteractionHarness& harness,
                               TestRadioButton& first,
                               TestRadioButton& second,
                               const ImVec2& mousePos = test::ImGuiInteractionHarness::offscreenMousePos(),
                               bool mouseDown = false)
{
    harness.beginFrame(mousePos, mouseDown);
    const test::InteractionRect firstRect = harness.render(first);
    const test::InteractionRect secondRect = harness.render(second);
    harness.endFrame();
    return RadioPairRects{firstRect, secondRect};
}

void warmUpRadio(test::ImGuiInteractionHarness& harness, TestRadioButton& radio)
{
    renderRadio(harness, radio);
    renderRadio(harness, radio);
}

void warmUpRadioPair(test::ImGuiInteractionHarness& harness, TestRadioButton& first, TestRadioButton& second)
{
    renderRadioPair(harness, first, second);
    renderRadioPair(harness, first, second);
}

}  // namespace

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

TEST_F(RadioButtonFixture, mouseClickSelectsRadioButtonAndEmitsSignal)
{
    test::ImGuiInteractionHarness harness;
    TestRadioButton radio("A");
    int signalCount = 0;
    bool lastState = false;
    radio.stateChanged.connect([&](bool selected) {
        ++signalCount;
        lastState = selected;
    });

    warmUpRadio(harness, radio);
    const test::InteractionRect rect = renderRadio(harness, radio);
    const ImVec2 center = test::ImGuiInteractionHarness::centerOf(rect);

    const test::InteractionRect downRect = renderRadio(harness, radio, center, true);
    EXPECT_TRUE(downRect.hovered);
    EXPECT_TRUE(downRect.active);
    EXPECT_FALSE(radio.isSelected());

    const test::InteractionRect upRect = renderRadio(harness, radio, center, false);
    EXPECT_TRUE(upRect.hovered);

    EXPECT_TRUE(radio.isSelected());
    EXPECT_EQ(signalCount, 1);
    EXPECT_TRUE(lastState);
}

TEST_F(RadioButtonFixture, linkedRadioButtonsAreExclusiveWhenClicked)
{
    test::ImGuiInteractionHarness harness;
    TestRadioButton a("A");
    TestRadioButton b("B");
    a.link(&b);
    a.setSelected(true);

    int aSignalCount = 0;
    int bSignalCount = 0;
    bool aLastState = true;
    bool bLastState = false;
    a.stateChanged.connect([&](bool selected) {
        ++aSignalCount;
        aLastState = selected;
    });
    b.stateChanged.connect([&](bool selected) {
        ++bSignalCount;
        bLastState = selected;
    });

    warmUpRadioPair(harness, a, b);
    const RadioPairRects rects = renderRadioPair(harness, a, b);
    const ImVec2 bCenter = test::ImGuiInteractionHarness::centerOf(rects.second);

    renderRadioPair(harness, a, b, bCenter, true);
    renderRadioPair(harness, a, b, bCenter, false);

    EXPECT_FALSE(a.isSelected());
    EXPECT_TRUE(b.isSelected());
    EXPECT_EQ(aSignalCount, 1);
    EXPECT_FALSE(aLastState);
    EXPECT_EQ(bSignalCount, 1);
    EXPECT_TRUE(bLastState);
}

TEST_F(RadioButtonFixture, disabledRadioButtonIgnoresMouseClick)
{
    test::ImGuiInteractionHarness harness;
    TestRadioButton radio("A");
    int signalCount = 0;
    radio.stateChanged.connect([&](bool) { ++signalCount; });

    warmUpRadio(harness, radio);
    const test::InteractionRect rect = renderRadio(harness, radio);
    const ImVec2 center = test::ImGuiInteractionHarness::centerOf(rect);

    renderRadio(harness, radio, center, true);
    renderRadio(harness, radio, center, false);
    EXPECT_TRUE(radio.isSelected());
    EXPECT_EQ(signalCount, 1);

    radio.setSelected(false);
    radio.setDisabled(true);

    renderRadio(harness, radio, center, true);
    renderRadio(harness, radio, center, false);

    EXPECT_FALSE(radio.isSelected());
    EXPECT_EQ(signalCount, 1);
}
