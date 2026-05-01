#include <gtest/gtest.h>
#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/SpinBox.h"
#include <SNFCore/Application.h>

#include "imgui.h"

using namespace snf::widgets;

namespace {

class RecordingSpinBox final : public SpinBox
{
public:
    using SpinBox::SpinBox;

    const test::InteractionRect& lastRect() const { return m_lastRect; }

    void renderConstrainedForTest(float width)
    {
        renderImGuiConstrained(width, -1.0f);
    }

protected:
    void renderImGuiConstrained(float width, float height) override
    {
        SpinBox::renderImGuiConstrained(width, height);
        m_lastRect = test::InteractionRect{
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::IsItemHovered(),
            ImGui::IsItemActive()};
    }

private:
    test::InteractionRect m_lastRect;
};

void renderSpinBox(test::ImGuiInteractionHarness& harness, RecordingSpinBox& spin, float width)
{
    harness.beginFrame();
    spin.renderConstrainedForTest(width);
    harness.endFrame();
}

}  // namespace

class SpinBoxFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(SpinBoxFixture, defaultState)
{
    SpinBox spin("Count", 0, 100);
    EXPECT_EQ(spin.minimum(), 0);
    EXPECT_EQ(spin.maximum(), 100);
    EXPECT_EQ(spin.value(), 0);
    EXPECT_EQ(spin.step(), 1);
    EXPECT_EQ(spin.textPlacement(), SpinBox::TextPlacement::Left);
    EXPECT_TRUE(spin.buttonsVisible());
}

TEST_F(SpinBoxFixture, parentOnlyConstruction)
{
    SpinBox spin;

    EXPECT_EQ(spin.label(), "");
    EXPECT_EQ(spin.minimum(), 0);
    EXPECT_EQ(spin.maximum(), 100);
    EXPECT_EQ(spin.value(), 0);
}

TEST_F(SpinBoxFixture, rangeOnlyConstruction)
{
    SpinBox spin(10, 20);

    EXPECT_EQ(spin.label(), "");
    EXPECT_EQ(spin.minimum(), 10);
    EXPECT_EQ(spin.maximum(), 20);
    EXPECT_EQ(spin.value(), 10);
}

TEST_F(SpinBoxFixture, setLabel)
{
    SpinBox spin("Old", 0, 10);
    spin.setLabel("New");
    EXPECT_EQ(spin.label(), "New");
}

TEST_F(SpinBoxFixture, setValue)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(30);
    EXPECT_EQ(spin.value(), 30);
}

TEST_F(SpinBoxFixture, setValueClampBelow)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(-1);
    EXPECT_EQ(spin.value(), 0);
}

TEST_F(SpinBoxFixture, setValueClampAbove)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(150);
    EXPECT_EQ(spin.value(), 100);
}

TEST_F(SpinBoxFixture, setRange)
{
    SpinBox spin("s", 0, 100);
    spin.setValue(80);
    spin.setRange(5, 50);
    EXPECT_EQ(spin.minimum(), 5);
    EXPECT_EQ(spin.maximum(), 50);
    EXPECT_EQ(spin.value(), 50);
}

TEST_F(SpinBoxFixture, invalidRangeSwapped)
{
    SpinBox spin("s", 100, 0);
    EXPECT_EQ(spin.minimum(), 0);
    EXPECT_EQ(spin.maximum(), 100);
}

TEST_F(SpinBoxFixture, setStep)
{
    SpinBox spin("s", 0, 100);
    spin.setStep(5);
    EXPECT_EQ(spin.step(), 5);
}

TEST_F(SpinBoxFixture, setStepZeroIgnored)
{
    SpinBox spin("s", 0, 100);
    spin.setStep(3);
    spin.setStep(0);   // Should be ignored.
    EXPECT_EQ(spin.step(), 3);
}

TEST_F(SpinBoxFixture, setStepNegativeIgnored)
{
    SpinBox spin("s", 0, 100);
    spin.setStep(2);
    spin.setStep(-1);  // Should be ignored.
    EXPECT_EQ(spin.step(), 2);
}

TEST_F(SpinBoxFixture, textPlacementCanBeConfigured)
{
    SpinBox spin("Count", 0, 100);

    spin.setTextPlacement(SpinBox::TextPlacement::Right);
    EXPECT_EQ(spin.textPlacement(), SpinBox::TextPlacement::Right);

    spin.setTextPlacement(SpinBox::TextPlacement::Hidden);
    EXPECT_EQ(spin.textPlacement(), SpinBox::TextPlacement::Hidden);
}

TEST_F(SpinBoxFixture, buttonsCanBeHidden)
{
    SpinBox spin("Count", 0, 100);

    spin.setButtonsVisible(false);

    EXPECT_FALSE(spin.buttonsVisible());
}

TEST_F(SpinBoxFixture, inputWidthsAreClamped)
{
    SpinBox spin("Count", 0, 100);

    spin.setMinimumInputWidth(-10.0f);
    spin.setPreferredInputWidth(-20.0f);

    EXPECT_FLOAT_EQ(spin.minimumInputWidth(), 0.0f);
    EXPECT_FLOAT_EQ(spin.preferredInputWidth(), 0.0f);

    spin.setMinimumInputWidth(48.0f);
    spin.setPreferredInputWidth(120.0f);

    EXPECT_FLOAT_EQ(spin.minimumInputWidth(), 48.0f);
    EXPECT_FLOAT_EQ(spin.preferredInputWidth(), 120.0f);
}

TEST_F(SpinBoxFixture, sizeHintHasDefaultInputWidth)
{
    test::ImGuiInteractionHarness harness;
    SpinBox spin("Port", 3000, 32000);

    harness.beginFrame();
    const Size hint = spin.sizeHint();
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    harness.endFrame();

    EXPECT_GE(hint.width, 48.0f);
    EXPECT_GT(hint.height, 0.0f);
}

TEST_F(SpinBoxFixture, hiddenLabelAndButtonsHiddenLetsInputUseFullWidth)
{
    test::ImGuiInteractionHarness harness;
    RecordingSpinBox spin("Count", 0, 100);
    spin.setTextPlacement(SpinBox::TextPlacement::Hidden);
    spin.setButtonsVisible(false);

    renderSpinBox(harness, spin, 220.0f);

    EXPECT_NEAR(spin.lastRect().min.x, 32.0f, 1.0f);
    EXPECT_NEAR(spin.lastRect().max.x - spin.lastRect().min.x, 220.0f, 1.0f);
}

TEST_F(SpinBoxFixture, leftLabelUsesNaturalWidth)
{
    test::ImGuiInteractionHarness harness;
    RecordingSpinBox spin("Count", 0, 100);
    spin.setButtonsVisible(false);

    renderSpinBox(harness, spin, 220.0f);

    EXPECT_GT(spin.lastRect().min.x, 32.0f);
    EXPECT_LT(spin.lastRect().max.x, 32.0f + 220.0f + 1.0f);
}

// Programmatic setter does NOT emit valueChanged.
TEST_F(SpinBoxFixture, setterDoesNotEmitSignal)
{
    SpinBox spin("s", 0, 100);
    int count = 0;
    spin.valueChanged.connect([&](int) { ++count; });
    spin.setValue(50);
    EXPECT_EQ(count, 0);
}
