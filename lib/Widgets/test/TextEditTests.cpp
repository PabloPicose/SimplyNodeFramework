#include <gtest/gtest.h>
#include "SNFWidgets/TextEdit.h"
#include <SNFCore/Application.h>

#include <string>

using namespace snf::widgets;

class TextEditFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(TextEditFixture, defaultConstruction)
{
    TextEdit edit;
    EXPECT_EQ(edit.text(), "");
    EXPECT_EQ(edit.label(), "");
}

TEST_F(TextEditFixture, constructionWithLabel)
{
    TextEdit edit("Notes##1");
    EXPECT_EQ(edit.label(), "Notes##1");
    EXPECT_EQ(edit.text(), "");
}

TEST_F(TextEditFixture, setText)
{
    TextEdit edit;
    edit.setText("line");
    EXPECT_EQ(edit.text(), "line");
}

TEST_F(TextEditFixture, setTextMultiline)
{
    TextEdit edit;
    edit.setText("Line 1\nLine 2\nLine 3");
    EXPECT_EQ(edit.text(), "Line 1\nLine 2\nLine 3");
}

TEST_F(TextEditFixture, setTextEmpty)
{
    TextEdit edit;
    edit.setText("something");
    edit.setText("");
    EXPECT_EQ(edit.text(), "");
}

TEST_F(TextEditFixture, clear)
{
    TextEdit edit;
    edit.setText("data\nmore");
    edit.clear();
    EXPECT_EQ(edit.text(), "");
}

TEST_F(TextEditFixture, setLabel)
{
    TextEdit edit("Old");
    edit.setLabel("New");
    EXPECT_EQ(edit.label(), "New");
}

TEST_F(TextEditFixture, longText)
{
    TextEdit edit;
    const std::string long_str(4096, 'y');
    edit.setText(long_str);
    EXPECT_EQ(edit.text(), long_str);
}

// Programmatic setter does NOT emit textChanged.
TEST_F(TextEditFixture, setterDoesNotEmitSignal)
{
    TextEdit edit;
    int count = 0;
    edit.textChanged.connect([&](const std::string&) { ++count; });
    edit.setText("abc\ndef");
    edit.clear();
    EXPECT_EQ(count, 0);
}
