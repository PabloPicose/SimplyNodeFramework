#pragma once

/**
 * @file LineEdit.h
 * @brief Single-line text input widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class LineEdit
 * @ingroup SNFWidgets
 * @brief A single-line text input whose content persists across frames.
 *
 * The internal buffer is managed by the widget; there is no ephemeral
 * per-frame allocation inside `renderImGui()`.  The buffer grows
 * automatically as the text nears capacity, so no small arbitrary limit
 * is imposed on the content.
 *
 * `textChanged` is emitted (with the new string) only when the text
 * effectively changes due to user editing.  Programmatic setters do not
 * emit signals.
 *
 * `editingFinished` is emitted when the user presses Enter to confirm
 * the input (detected via ImGui's InputText return value).
 *
 * @code
 * snf::widgets::LineEdit edit("##name", &win);
 * edit.setText("default");
 *
 * edit.textChanged.connect([](const std::string& t) {
 *     std::printf("Text: %s\n", t.c_str());
 * });
 * @endcode
 */
class LineEdit : public Widget
{
public:
    /**
     * @param label  ImGui label (use "##id" for a hidden label).
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit LineEdit(const std::string& label = std::string(), snf::Node* parent = nullptr);

    /** @brief Sets the text content (does not emit `textChanged`). */
    void setText(const std::string& text);

    /** @brief Returns the current text content. */
    std::string text() const;

    /** @brief Clears the text content (does not emit `textChanged`). */
    void clear();

    /** @brief Sets the ImGui label shown next to the input field. */
    void setLabel(const std::string& label);

    /** @brief Returns the current label. */
    std::string label() const;

    /**
     * @brief Emitted when the user edits the text and the content changes.
     *
     * The argument is the updated text string.
     */
    Signal<std::string> textChanged;

    /**
     * @brief Emitted when the user presses Enter to confirm the input.
     *
     * The argument is the current text at the moment Enter is pressed.
     */
    Signal<std::string> editingFinished;

protected:
    void renderImGui() override;

private:
    /** Minimum buffer capacity (grows automatically as needed). */
    static constexpr std::size_t k_initialCapacity = 256;

    /**
     * @brief Synchronises @p m_buffer with @p m_text and ensures spare
     *        capacity for further typing.
     */
    void syncBuffer();

    std::string        m_label;
    std::string        m_text;
    std::vector<char>  m_buffer;   ///< Mutable buffer passed to ImGui::InputText.
};

}  // namespace widgets
}  // namespace snf
