#include <gtest/gtest.h>
#include "SNFWidgets/Label.h"
#include <SNFCore/Application.h>

using namespace snf::widgets;

class LabelFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(LabelFixture, defaultConstruction)
{
    Label lbl;
    EXPECT_EQ(lbl.text(), "");
}

TEST_F(LabelFixture, constructionWithText)
{
    Label lbl("Hello");
    EXPECT_EQ(lbl.text(), "Hello");
}

TEST_F(LabelFixture, setText)
{
    Label lbl("Initial");
    lbl.setText("Updated");
    EXPECT_EQ(lbl.text(), "Updated");
}

TEST_F(LabelFixture, setTextEmpty)
{
    Label lbl("Something");
    lbl.setText("");
    EXPECT_EQ(lbl.text(), "");
}

TEST_F(LabelFixture, constructionWithParentNull)
{
    Label lbl("Test", nullptr);
    EXPECT_EQ(lbl.text(), "Test");
}
