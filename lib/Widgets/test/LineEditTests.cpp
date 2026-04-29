#include <gtest/gtest.h>
#include "SNFWidgets/LineEdit.h"
#include <SNFCore/Application.h>

#include <string>

using namespace snf::widgets;

class LineEditFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(LineEditFixture, defaultConstruction)
{
    LineEdit edit;
    EXPECT_EQ(edit.text(), "");
    EXPECT_EQ(edit.label(), "");
}

TEST_F(LineEditFixture, constructionWithLabel)
{
    LineEdit edit("Name##1");
    EXPECT_EQ(edit.label(), "Name##1");
    EXPECT_EQ(edit.text(), "");
}

TEST_F(LineEditFixture, setText)
{
    LineEdit edit;
    edit.setText("hello");
    EXPECT_EQ(edit.text(), "hello");
}

TEST_F(LineEditFixture, setTextEmpty)
{
    LineEdit edit;
    edit.setText("something");
    edit.setText("");
    EXPECT_EQ(edit.text(), "");
}

TEST_F(LineEditFixture, clear)
{
    LineEdit edit;
    edit.setText("data");
    edit.clear();
    EXPECT_EQ(edit.text(), "");
}

TEST_F(LineEditFixture, setLabel)
{
    LineEdit edit("Old");
    edit.setLabel("New");
    EXPECT_EQ(edit.label(), "New");
}

TEST_F(LineEditFixture, longText)
{
    LineEdit edit;
    const std::string long_str(2048, 'x');
    edit.setText(long_str);
    EXPECT_EQ(edit.text(), long_str);
}

TEST_F(LineEditFixture, placeholderAndAuxiliaryText)
{
    LineEdit edit;
    edit.setPlaceholder("Search");
    edit.setAuxiliaryText("required");

    EXPECT_EQ(edit.placeholder(), "Search");
    EXPECT_EQ(edit.auxiliaryText(), "required");
}

TEST_F(LineEditFixture, layoutPolicyCanBeConfigured)
{
    LineEdit edit;

    EXPECT_EQ(edit.layoutPolicy(), LineEdit::LayoutPolicy::InputExpands);

    edit.setLayoutPolicy(LineEdit::LayoutPolicy::TextPriority);
    EXPECT_EQ(edit.layoutPolicy(), LineEdit::LayoutPolicy::TextPriority);

    edit.setLayoutPolicy(LineEdit::LayoutPolicy::InputPriority);
    EXPECT_EQ(edit.layoutPolicy(), LineEdit::LayoutPolicy::InputPriority);

    edit.setLayoutPolicy(LineEdit::LayoutPolicy::FixedTextWidth);
    EXPECT_EQ(edit.layoutPolicy(), LineEdit::LayoutPolicy::FixedTextWidth);
}

TEST_F(LineEditFixture, textPlacementAndOverflowCanBeConfigured)
{
    LineEdit edit;

    EXPECT_EQ(edit.textPlacement(), LineEdit::TextPlacement::Right);
    EXPECT_EQ(edit.textOverflow(), LineEdit::TextOverflow::Clip);

    edit.setTextPlacement(LineEdit::TextPlacement::Left);
    edit.setTextOverflow(LineEdit::TextOverflow::Hide);

    EXPECT_EQ(edit.textPlacement(), LineEdit::TextPlacement::Left);
    EXPECT_EQ(edit.textOverflow(), LineEdit::TextOverflow::Hide);
}

TEST_F(LineEditFixture, inputAndCompanionWidthsAreClamped)
{
    LineEdit edit;

    edit.setMinimumInputWidth(-10.0f);
    edit.setPreferredInputWidth(-20.0f);
    edit.setCompanionTextWidth(-30.0f);

    EXPECT_FLOAT_EQ(edit.minimumInputWidth(), 0.0f);
    EXPECT_FLOAT_EQ(edit.preferredInputWidth(), 0.0f);
    EXPECT_FLOAT_EQ(edit.companionTextWidth(), 0.0f);

    edit.setMinimumInputWidth(64.0f);
    edit.setPreferredInputWidth(160.0f);
    edit.setCompanionTextWidth(80.0f);

    EXPECT_FLOAT_EQ(edit.minimumInputWidth(), 64.0f);
    EXPECT_FLOAT_EQ(edit.preferredInputWidth(), 160.0f);
    EXPECT_FLOAT_EQ(edit.companionTextWidth(), 80.0f);
}

TEST_F(LineEditFixture, emptyAndLongTextKeepLayoutConfiguration)
{
    LineEdit edit("Name##id");
    edit.setAuxiliaryText(std::string(512, 'a'));
    edit.setText("");
    edit.setLayoutPolicy(LineEdit::LayoutPolicy::InputPriority);
    edit.setMinimumInputWidth(24.0f);

    EXPECT_EQ(edit.text(), "");
    EXPECT_EQ(edit.layoutPolicy(), LineEdit::LayoutPolicy::InputPriority);
    EXPECT_FLOAT_EQ(edit.minimumInputWidth(), 24.0f);

    const std::string long_str(4096, 'x');
    edit.setText(long_str);
    EXPECT_EQ(edit.text(), long_str);
    EXPECT_EQ(edit.auxiliaryText(), std::string(512, 'a'));
}

// Programmatic setter does NOT emit textChanged.
TEST_F(LineEditFixture, setterDoesNotEmitSignal)
{
    LineEdit edit;
    int count = 0;
    edit.textChanged.connect([&](const std::string&) { ++count; });
    edit.setText("abc");
    edit.clear();
    EXPECT_EQ(count, 0);
}
