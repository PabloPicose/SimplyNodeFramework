#include "SNFWidgets/MessageBox.h"

#include "imgui.h"  // internal to SNFWidgets, not part of the public API

namespace snf {
namespace widgets {

MessageBox::MessageBox(const std::string& title, snf::Node* parent)
    : Widget(parent), m_title(title)
{
}

void MessageBox::setTitle(const std::string& title)
{
    m_title = title;
}

std::string MessageBox::title() const
{
    return m_title;
}

void MessageBox::setText(const std::string& text)
{
    m_text = text;
}

std::string MessageBox::text() const
{
    return m_text;
}

void MessageBox::setButtons(const std::vector<Button>& buttons)
{
    m_buttons = buttons.empty() ? std::vector<Button>{Button::Accept} : buttons;
}

std::vector<MessageBox::Button> MessageBox::buttons() const
{
    return m_buttons;
}

void MessageBox::open()
{
    m_result = Result::None;
    m_open = true;
    m_openRequested = true;
}

void MessageBox::close()
{
    finish(Result::None);
}

bool MessageBox::isOpen() const
{
    return m_open;
}

MessageBox::Result MessageBox::result() const
{
    return m_result;
}

void MessageBox::accept()
{
    finish(Result::Accepted);
}

void MessageBox::reject()
{
    finish(Result::Rejected);
}

void MessageBox::cancel()
{
    finish(Result::Canceled);
}

void MessageBox::yes()
{
    finish(Result::Yes);
}

void MessageBox::no()
{
    finish(Result::No);
}

void MessageBox::finish(Result result)
{
    if (! m_open && m_result == result) {
        return;
    }

    m_result = result;
    const bool wasOpen = m_open;
    m_open = false;
    m_openRequested = false;
    if (wasOpen) {
        finished.emit(m_result);
    }
}

const char* MessageBox::buttonText(Button button)
{
    switch (button) {
    case Button::Accept:
        return "OK";
    case Button::Cancel:
        return "Cancel";
    case Button::Yes:
        return "Yes";
    case Button::No:
        return "No";
    }

    return "OK";
}

MessageBox::Result MessageBox::resultForButton(Button button)
{
    switch (button) {
    case Button::Accept:
        return Result::Accepted;
    case Button::Cancel:
        return Result::Canceled;
    case Button::Yes:
        return Result::Yes;
    case Button::No:
        return Result::No;
    }

    return Result::None;
}

void MessageBox::renderImGui()
{
    if (! m_open) {
        return;
    }

    if (m_openRequested) {
        ImGui::OpenPopup(m_title.c_str());
        m_openRequested = false;
    }

    bool open = m_open;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginPopupModal(m_title.c_str(), &open, flags)) {
        if (! m_text.empty()) {
            ImGui::TextWrapped("%s", m_text.c_str());
            ImGui::Spacing();
        }

        for (std::size_t i = 0; i < m_buttons.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine();
            }

            const Button button = m_buttons[i];
            if (ImGui::Button(buttonText(button))) {
                finish(resultForButton(button));
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    if (! open && m_open) {
        finish(Result::Canceled);
    }
}

}  // namespace widgets
}  // namespace snf
