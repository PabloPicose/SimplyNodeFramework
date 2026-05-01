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
 * snf::widgets::LineEdit edit(&win);
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
     * @enum LayoutPolicy
     * @brief Controls how the editable field and companion text share width.
     *
     * `InputExpands` keeps the input useful first and clips companion text
     * when the available layout width becomes narrow.
     * `TextPriority` preserves label/auxiliary text first and gives the input
     * the remaining width.
     * `InputPriority` gives the input its preferred width first and clips or
     * hides companion text with the remaining width.
     * `FixedTextWidth` reserves `companionTextWidth()` for label/auxiliary
     * text and lets the input use the rest.
     */
    enum class LayoutPolicy {
        InputExpands,
        TextPriority,
        InputPriority,
        FixedTextWidth,
    };

    /**
     * @enum TextPlacement
     * @brief Placement for the visible part of `label()`.
     *
     * The input itself always uses a hidden ImGui id. The label text is only
     * rendered as side text when this placement is `Left` or `Right`.
     */
    enum class TextPlacement {
        Hidden,
        Left,
        Right,
    };

    /**
     * @enum TextOverflow
     * @brief Behaviour for label/auxiliary text that does not fit.
     */
    enum class TextOverflow {
        Clip,
        Hide,
    };

    /** @brief Creates an empty line edit. */
    explicit LineEdit(snf::Node* parent = nullptr);

    /**
     * @param label  Optional side text. Hidden by default; call
     *               `setTextPlacement()` to render it.
     * @param parent Parent node (a Window or another container Widget).
     */
    explicit LineEdit(const std::string& label, snf::Node* parent = nullptr);

    /** @brief Sets the text content (does not emit `textChanged`). */
    void setText(const std::string& text);

    /** @brief Returns the current text content. */
    std::string text() const;

    /** @brief Clears the text content (does not emit `textChanged`). */
    void clear();

    /** @brief Sets optional side text for the input field. */
    void setLabel(const std::string& label);

    /** @brief Returns the current label. */
    std::string label() const;

    /** @brief Sets placeholder text shown inside the empty input field. */
    void setPlaceholder(const std::string& placeholder);

    /** @brief Returns the placeholder text. */
    std::string placeholder() const;

    /** @brief Sets auxiliary text rendered to the right of the editable field. */
    void setAuxiliaryText(const std::string& text);

    /** @brief Returns the auxiliary text. */
    std::string auxiliaryText() const;

    /** @brief Sets the width-sharing policy used in constrained layouts. */
    void setLayoutPolicy(LayoutPolicy policy);

    /** @brief Returns the current width-sharing policy. */
    LayoutPolicy layoutPolicy() const;

    /** @brief Sets where the visible label text is rendered. */
    void setTextPlacement(TextPlacement placement);

    /** @brief Returns where the visible label text is rendered. */
    TextPlacement textPlacement() const;

    /** @brief Sets how companion text behaves when it does not fit. */
    void setTextOverflow(TextOverflow overflow);

    /** @brief Returns the current companion text overflow mode. */
    TextOverflow textOverflow() const;

    /**
     * @brief Sets the minimum editable-field width in pixels.
     *
     * Values below zero are clamped to zero. A zero minimum lets the layout
     * use the widget's built-in default minimum.
     */
    void setMinimumInputWidth(float width);

    /** @brief Returns the configured minimum editable-field width. */
    float minimumInputWidth() const;

    /**
     * @brief Sets the preferred editable-field width in pixels.
     *
     * A value of zero means "use the available width after companion text".
     */
    void setPreferredInputWidth(float width);

    /** @brief Returns the configured preferred editable-field width. */
    float preferredInputWidth() const;

    /**
     * @brief Sets the reserved width for label/auxiliary text in pixels.
     *
     * This value is used by `LayoutPolicy::FixedTextWidth`; zero falls back to
     * the natural measured text width.
     */
    void setCompanionTextWidth(float width);

    /** @brief Returns the configured companion text width. */
    float companionTextWidth() const;

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

    Size sizeHint() const override;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    /** Minimum buffer capacity (grows automatically as needed). */
    static constexpr std::size_t k_initialCapacity = 256;

    /**
     * @brief Synchronises @p m_buffer with @p m_text and ensures spare
     *        capacity for further typing.
     */
    void syncBuffer();
    void renderWithAvailableWidth(float width);
    bool renderInputText(float width);
    bool renderCompanionText(const std::string& text, float width) const;
    float effectiveMinimumInputWidth() const;

    std::string        m_label;
    std::string        m_text;
    std::string        m_placeholder;
    std::string        m_auxiliaryText;
    std::vector<char>  m_buffer;   ///< Mutable buffer passed to ImGui::InputText.
    LayoutPolicy       m_layoutPolicy = LayoutPolicy::InputExpands;
    TextPlacement      m_textPlacement = TextPlacement::Hidden;
    TextOverflow       m_textOverflow = TextOverflow::Clip;
    float              m_minimumInputWidth = 0.0f;
    float              m_preferredInputWidth = 0.0f;
    float              m_companionTextWidth = 0.0f;
};

}  // namespace widgets
}  // namespace snf
