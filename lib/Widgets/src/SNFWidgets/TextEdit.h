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
 * Word-wrap is **enabled by default**.  The following options can be toggled
 * independently:
 *   - `wordWrap`          — wrap long lines (default: `true`)
 *   - `readOnly`          — prevent user edits, useful for display (default: `false`)
 *   - `allowTabInput`     — insert `'\t'` on Tab key, useful for code (default: `false`)
 *   - `ctrlEnterForNewLine` — Enter confirms, Ctrl+Enter adds newline (default: `false`)
 *   - `autoSelectAll`     — select all text when the widget gains focus (default: `false`)
 *
 * @code
 * snf::widgets::TextEdit edit(&win);
 * edit.setText("Line 1\nLine 2");
 * edit.setWordWrap(true);  // already the default
 *
 * edit.textChanged.connect([](const std::string& t) {
 *     std::printf("Content: %s\n", t.c_str());
 * });
 * @endcode
 */
class TextEdit : public Widget
{
public:
    /** @brief Constructs a TextEdit with no visible label. */
    explicit TextEdit(snf::Node* parent = nullptr);

    /**
     * @param label  Optional visible label shown above the input area.
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit TextEdit(const std::string& label, snf::Node* parent = nullptr);

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

    // -------------------------------------------------------------------------
    // Input behaviour options
    // -------------------------------------------------------------------------

    /**
     * @brief Enables or disables word-wrap for lines that exceed the widget width.
     *
     * Corresponds to `ImGuiInputTextFlags_WordWrap`. Default: `true`.
     */
    void setWordWrap(bool enabled);

    /** @brief Returns `true` when word-wrap is active. */
    bool wordWrap() const;

    /**
     * @brief Makes the widget read-only (prevents user edits).
     *
     * Useful for displaying text without allowing modifications.
     * Corresponds to `ImGuiInputTextFlags_ReadOnly`. Default: `false`.
     */
    void setReadOnly(bool readOnly);

    /** @brief Returns `true` when the widget is in read-only mode. */
    bool readOnly() const;

    /**
     * @brief Allows inserting a tab character (`'\t'`) via the Tab key.
     *
     * Useful for code-editor scenarios. Corresponds to
     * `ImGuiInputTextFlags_AllowTabInput`. Default: `false`.
     */
    void setAllowTabInput(bool allow);

    /** @brief Returns `true` when Tab key inserts a tab character. */
    bool allowTabInput() const;

    /**
     * @brief Swaps the roles of Enter and Ctrl+Enter.
     *
     * When `true`: Enter confirms/submits and Ctrl+Enter inserts a new line.
     * When `false` (default): Enter inserts a new line and Ctrl+Enter confirms.
     * Corresponds to `ImGuiInputTextFlags_CtrlEnterForNewLine`. Default: `false`.
     */
    void setCtrlEnterForNewLine(bool enabled);

    /** @brief Returns `true` when Ctrl+Enter is used to insert new lines. */
    bool ctrlEnterForNewLine() const;

    /**
     * @brief Selects all text automatically when the widget gains mouse focus.
     *
     * Corresponds to `ImGuiInputTextFlags_AutoSelectAll`. Default: `false`.
     */
    void setAutoSelectAll(bool enabled);

    /** @brief Returns `true` when auto-select-all on focus is active. */
    bool autoSelectAll() const;

    // -------------------------------------------------------------------------

    /**
     * @brief Emitted when the user edits the text and the content changes.
     *
     * The argument is the updated text string.
     */
    Signal<std::string> textChanged;

    Size sizeHint() const override;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    /** Minimum buffer capacity (grows automatically as needed). */
    static constexpr std::size_t k_initialCapacity = 1024;

    /**
     * @brief Synchronises @p m_buffer with @p m_text and ensures spare
     *        capacity for further typing.
     */
    void syncBuffer();
    void renderInput(float width, float height);
    int  buildFlags() const;  ///< Builds the ImGuiInputTextFlags bitmask from current settings.

    std::string        m_label;
    std::string        m_text;
    std::vector<char>  m_buffer;   ///< Mutable buffer passed to ImGui::InputTextMultiline.

    bool m_wordWrap          = true;
    bool m_readOnly          = false;
    bool m_allowTabInput     = false;
    bool m_ctrlEnterForNewLine = false;
    bool m_autoSelectAll     = false;
};

}  // namespace widgets
}  // namespace snf
