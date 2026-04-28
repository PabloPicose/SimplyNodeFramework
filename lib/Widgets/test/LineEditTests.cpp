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
