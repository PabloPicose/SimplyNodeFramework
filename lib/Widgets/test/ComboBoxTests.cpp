#include <gtest/gtest.h>
#include "SNFWidgets/ComboBox.h"
#include <SNFCore/Application.h>

#include <string>

using namespace snf::widgets;

class ComboBoxFixture : public ::testing::Test
{
public:
    void SetUp() override   { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }
    snf::Application* app = nullptr;
};

TEST_F(ComboBoxFixture, defaultConstruction)
{
    ComboBox cb("Mode");
    EXPECT_EQ(cb.label(), "Mode");
    EXPECT_EQ(cb.count(), 0);
    EXPECT_EQ(cb.currentIndex(), -1);
    EXPECT_EQ(cb.currentText(), "");
}

TEST_F(ComboBoxFixture, addItem)
{
    ComboBox cb;
    cb.addItem("Alpha");
    EXPECT_EQ(cb.count(), 1);
    EXPECT_EQ(cb.currentIndex(), 0);
    EXPECT_EQ(cb.currentText(), "Alpha");
}

TEST_F(ComboBoxFixture, addItems)
{
    ComboBox cb;
    cb.addItems({"A", "B", "C"});
    EXPECT_EQ(cb.count(), 3);
    EXPECT_EQ(cb.currentIndex(), 0);
    EXPECT_EQ(cb.currentText(), "A");
}

TEST_F(ComboBoxFixture, setCurrentIndexValid)
{
    ComboBox cb;
    cb.addItems({"A", "B", "C"});
    cb.setCurrentIndex(2);
    EXPECT_EQ(cb.currentIndex(), 2);
    EXPECT_EQ(cb.currentText(), "C");
}

TEST_F(ComboBoxFixture, setCurrentIndexMinusOne)
{
    ComboBox cb;
    cb.addItems({"A", "B"});
    cb.setCurrentIndex(-1);
    EXPECT_EQ(cb.currentIndex(), -1);
    EXPECT_EQ(cb.currentText(), "");
}

TEST_F(ComboBoxFixture, setCurrentIndexOutOfRangeIgnored)
{
    ComboBox cb;
    cb.addItems({"A", "B"});
    cb.setCurrentIndex(0);
    cb.setCurrentIndex(99);  // Out of range — ignored.
    EXPECT_EQ(cb.currentIndex(), 0);
}

TEST_F(ComboBoxFixture, clearItems)
{
    ComboBox cb;
    cb.addItems({"A", "B"});
    cb.clearItems();
    EXPECT_EQ(cb.count(), 0);
    EXPECT_EQ(cb.currentIndex(), -1);
    EXPECT_EQ(cb.currentText(), "");
}

TEST_F(ComboBoxFixture, currentTextEmptyWhenNoSelection)
{
    ComboBox cb;
    EXPECT_EQ(cb.currentText(), "");
}

// Programmatic setter does NOT emit signals.
TEST_F(ComboBoxFixture, setterDoesNotEmitSignals)
{
    ComboBox cb;
    cb.addItems({"A", "B", "C"});
    int indexCount = 0;
    int textCount  = 0;
    cb.currentIndexChanged.connect([&](int) { ++indexCount; });
    cb.currentTextChanged.connect([&](const std::string&) { ++textCount; });

    cb.setCurrentIndex(1);
    cb.setCurrentIndex(2);
    EXPECT_EQ(indexCount, 0);
    EXPECT_EQ(textCount, 0);
}

TEST_F(ComboBoxFixture, setLabel)
{
    ComboBox cb("Old");
    cb.setLabel("New");
    EXPECT_EQ(cb.label(), "New");
}
