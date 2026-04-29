#pragma once

/**
 * @file MessageBox.h
 * @brief ImGui-backed asynchronous modal message box.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <SNFCore/Connection.h>

#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class MessageBox
 * @ingroup SNFWidgets
 * @brief Simple modal dialog with configurable standard buttons.
 *
 * MessageBox is state based and non-blocking. Call `open()` to request that
 * the modal appears during the next frame, connect to `finished`, and inspect
 * `result()` after the user chooses a button. It does not expose Dear ImGui
 * types and does not depend on native OS dialogs.
 */
class MessageBox : public Widget
{
public:
    /** @brief Standard buttons supported by MessageBox. */
    enum class Button {
        Accept,
        Cancel,
        Yes,
        No,
    };

    /** @brief Result selected by the user, or `None` before completion. */
    enum class Result {
        None,
        Accepted,
        Rejected,
        Canceled,
        Yes,
        No,
    };

    explicit MessageBox(const std::string& title = std::string(), snf::Node* parent = nullptr);

    /** @brief Sets the modal title. */
    void setTitle(const std::string& title);

    /** @brief Returns the modal title. */
    std::string title() const;

    /** @brief Sets the message body. */
    void setText(const std::string& text);

    /** @brief Returns the message body. */
    std::string text() const;

    /** @brief Sets the buttons shown by the dialog, in display order. */
    void setButtons(const std::vector<Button>& buttons);

    /** @brief Returns the configured buttons. */
    std::vector<Button> buttons() const;

    /** @brief Opens the modal during the next render frame. */
    void open();

    /** @brief Closes the modal without choosing a standard answer. */
    void close();

    /** @brief Returns whether the modal is logically open. */
    bool isOpen() const;

    /** @brief Returns the most recent result. */
    Result result() const;

    /** @brief Completes the dialog with `Result::Accepted`. */
    void accept();

    /** @brief Completes the dialog with `Result::Rejected`. */
    void reject();

    /** @brief Completes the dialog with `Result::Canceled`. */
    void cancel();

    /** @brief Completes the dialog with `Result::Yes`. */
    void yes();

    /** @brief Completes the dialog with `Result::No`. */
    void no();

    /** @brief Emitted once when the dialog finishes or is closed. */
    Signal<Result> finished;

protected:
    void renderImGui() override;

private:
    void finish(Result result);
    static const char* buttonText(Button button);
    static Result resultForButton(Button button);

    std::string m_title;
    std::string m_text;
    std::vector<Button> m_buttons{Button::Accept};
    Result      m_result = Result::None;
    bool        m_open = false;
    bool        m_openRequested = false;
};

}  // namespace widgets
}  // namespace snf
