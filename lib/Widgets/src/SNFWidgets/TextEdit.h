#pragma once

/**
 * @file TextEdit.h
 * @brief Multi-line text input widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class TextEdit
 * @ingroup SNFWidgets
 * @brief A multi-line text editor whose content persists across frames.
 *
 * Supports embedded newline characters (`'\n'`).  The internal buffer is
 * managed by the widget; there is no ephemeral per-frame allocation inside
 * `renderImGui()`.  The buffer grows automatically as the text nears capacity.
 *
 * `textChanged` is emitted (with the new string) only when the text
 * effectively changes due to user editing.  Programmatic setters do not
 * emit signals.
 *
 * @code
 * snf::widgets::TextEdit edit("##notes", &win);
 * edit.setText("Line 1\nLine 2");
 *
 * edit.textChanged.connect([](const std::string& t) {
 *     std::printf("Content: %s\n", t.c_str());
 * });
 * @endcode
 */
class TextEdit : public Widget
{
public:
    /**
     * @param label  ImGui label (use "##id" for a hidden label).
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit TextEdit(const std::string& label = std::string(), snf::Node* parent = nullptr);

    /** @brief Sets the text content (does not emit `textChanged`). */
    void setText(const std::string& text);

    /** @brief Returns the current text content. */
    std::string text() const;

    /** @brief Clears the text content (does not emit `textChanged`). */
    void clear();

    /** @brief Sets the ImGui label shown above the input area. */
    void setLabel(const std::string& label);

    /** @brief Returns the current label. */
    std::string label() const;

    /**
     * @brief Emitted when the user edits the text and the content changes.
     *
     * The argument is the updated text string.
     */
    Signal<std::string> textChanged;

protected:
    void renderImGui() override;

private:
    /** Minimum buffer capacity (grows automatically as needed). */
    static constexpr std::size_t k_initialCapacity = 1024;

    /**
     * @brief Synchronises @p m_buffer with @p m_text and ensures spare
     *        capacity for further typing.
     */
    void syncBuffer();

    std::string        m_label;
    std::string        m_text;
    std::vector<char>  m_buffer;   ///< Mutable buffer passed to ImGui::InputTextMultiline.
};

}  // namespace widgets
}  // namespace snf
