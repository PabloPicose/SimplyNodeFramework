#include <gtest/gtest.h>

#include "SNFWidgets/MessageBox.h"

#include <SNFCore/Application.h>

#include <vector>

using namespace snf::widgets;

class MessageBoxFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new snf::Application(0, nullptr); }
    void TearDown() override { delete app; app = nullptr; }

    snf::Application* app = nullptr;
};

TEST_F(MessageBoxFixture, defaultState)
{
    MessageBox box("Confirm");

    EXPECT_EQ(box.title(), "Confirm");
    EXPECT_EQ(box.text(), "");
    ASSERT_EQ(box.buttons().size(), 1u);
    EXPECT_EQ(box.buttons()[0], MessageBox::Button::Accept);
    EXPECT_EQ(box.result(), MessageBox::Result::None);
    EXPECT_FALSE(box.isOpen());
}

TEST_F(MessageBoxFixture, titleTextAndButtons)
{
    MessageBox box;

    box.setTitle("Delete");
    box.setText("Delete selected item?");
    box.setButtons({MessageBox::Button::Yes, MessageBox::Button::No, MessageBox::Button::Cancel});

    EXPECT_EQ(box.title(), "Delete");
    EXPECT_EQ(box.text(), "Delete selected item?");
    EXPECT_EQ(
        box.buttons(),
        (std::vector<MessageBox::Button>{
            MessageBox::Button::Yes,
            MessageBox::Button::No,
            MessageBox::Button::Cancel}));
}

TEST_F(MessageBoxFixture, emptyButtonsFallbackToAccept)
{
    MessageBox box;

    box.setButtons({});

    ASSERT_EQ(box.buttons().size(), 1u);
    EXPECT_EQ(box.buttons()[0], MessageBox::Button::Accept);
}

TEST_F(MessageBoxFixture, openAndClose)
{
    MessageBox box("Confirm");
    int finishedCount = 0;
    MessageBox::Result lastResult = MessageBox::Result::Accepted;
    box.finished.connect([&](MessageBox::Result result) {
        ++finishedCount;
        lastResult = result;
    });

    box.open();
    EXPECT_TRUE(box.isOpen());
    EXPECT_EQ(box.result(), MessageBox::Result::None);

    box.close();
    EXPECT_FALSE(box.isOpen());
    EXPECT_EQ(box.result(), MessageBox::Result::None);
    EXPECT_EQ(finishedCount, 1);
    EXPECT_EQ(lastResult, MessageBox::Result::None);
}

TEST_F(MessageBoxFixture, acceptCancelRejectResults)
{
    MessageBox box("Confirm");
    std::vector<MessageBox::Result> results;
    box.finished.connect([&](MessageBox::Result result) { results.push_back(result); });

    box.open();
    box.accept();
    EXPECT_FALSE(box.isOpen());
    EXPECT_EQ(box.result(), MessageBox::Result::Accepted);

    box.open();
    box.cancel();
    EXPECT_EQ(box.result(), MessageBox::Result::Canceled);

    box.open();
    box.reject();
    EXPECT_EQ(box.result(), MessageBox::Result::Rejected);

    EXPECT_EQ(
        results,
        (std::vector<MessageBox::Result>{
            MessageBox::Result::Accepted,
            MessageBox::Result::Canceled,
            MessageBox::Result::Rejected}));
}

TEST_F(MessageBoxFixture, yesNoResults)
{
    MessageBox box("Question");
    std::vector<MessageBox::Result> results;
    box.finished.connect([&](MessageBox::Result result) { results.push_back(result); });

    box.open();
    box.yes();
    EXPECT_EQ(box.result(), MessageBox::Result::Yes);

    box.open();
    box.no();
    EXPECT_EQ(box.result(), MessageBox::Result::No);

    EXPECT_EQ(
        results,
        (std::vector<MessageBox::Result>{
            MessageBox::Result::Yes,
            MessageBox::Result::No}));
}

TEST_F(MessageBoxFixture, finishingClosedBoxDoesNotEmit)
{
    MessageBox box("Confirm");
    int finishedCount = 0;
    box.finished.connect([&](MessageBox::Result) { ++finishedCount; });

    box.accept();
    box.cancel();
    box.close();

    EXPECT_EQ(finishedCount, 0);
    EXPECT_FALSE(box.isOpen());
}
