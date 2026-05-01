#include <gtest/gtest.h>
#include "ImGuiInteractionHarness.h"
#include "SNFWidgets/TextEdit.h"
#include <SNFCore/Application.h>

#include "imgui.h"

#include <string>

using namespace snf::widgets;

namespace {

class RecordingTextEdit final : public TextEdit
{
public:
    using TextEdit::TextEdit;

    float lastWidth() const { return m_lastWidth; }
    float lastHeight() const { return m_lastHeight; }

    void renderConstrainedForTest(float width, float height)
    {
        renderImGuiConstrained(width, height);
    }

protected:
    void renderImGuiConstrained(float width, float height) override
    {
        TextEdit::renderImGuiConstrained(width, height);
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        m_lastWidth = max.x - min.x;
        m_lastHeight = max.y - min.y;
    }

private:
    float m_lastWidth = 0.0f;
    float m_lastHeight = 0.0f;
};

}  // namespace

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

TEST_F(TextEditFixture, constrainedRenderUsesProvidedSize)
{
    snf::widgets::test::ImGuiInteractionHarness harness;
    RecordingTextEdit edit;

    harness.beginFrame();
    edit.renderConstrainedForTest(320.0f, 180.0f);
    harness.endFrame();

    EXPECT_NEAR(edit.lastWidth(), 320.0f, 1.0f);
    EXPECT_NEAR(edit.lastHeight(), 180.0f, 1.0f);
}
