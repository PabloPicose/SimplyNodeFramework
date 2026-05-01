#pragma once

/**
 * @file SpinBox.h
 * @brief Integer spin box widget with increment/decrement controls.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>
#include <vector>

namespace snf {
namespace widgets {

/**
 * @class SpinBox
 * @ingroup SNFWidgets
 * @brief A numeric input with optional side text and step controls.
 *
 * `valueChanged` is emitted (with the new integer value) only when the
 * effective value changes due to user interaction.  Programmatic setters do
 * not emit signals.  Values outside [minimum, maximum] are clamped.
 *
 * If `minimum > maximum` the endpoints are swapped automatically.
 * A `step <= 0` is silently ignored; the step remains unchanged.
 *
 * @code
 * snf::widgets::SpinBox spin("Count", 0, 10, &win);
 * spin.setStep(2);
 *
 * spin.valueChanged.connect([](int v) {
 *     std::printf("Count: %d\n", v);
 * });
 * @endcode
 */
class SpinBox : public Widget
{
public:
    enum class TextPlacement {
        Hidden,
        Left,
        Right,
    };

    explicit SpinBox(snf::Node* parent = nullptr);
    explicit SpinBox(int minimum, int maximum, snf::Node* parent = nullptr);

    /**
     * @param label   Optional side text shown next to the spin box.
     * @param minimum Lower bound (inclusive).
     * @param maximum Upper bound (inclusive).
     * @param parent  Parent node (a Window or another container Widget).
     *
     * If `minimum > maximum` the endpoints are swapped automatically.
     */
    explicit SpinBox(const std::string& label,
                     int minimum = 0,
                     int maximum = 100,
                     snf::Node* parent = nullptr);

    /** @brief Sets the spin box label. */
    void setLabel(const std::string& label);

    /** @brief Returns the current label. */
    std::string label() const;

    /** @brief Sets where the optional side text is rendered. */
    void setTextPlacement(TextPlacement placement);

    /** @brief Returns where the optional side text is rendered. */
    TextPlacement textPlacement() const;

    /** @brief Shows or hides the increment/decrement buttons. */
    void setButtonsVisible(bool visible);

    /** @brief Returns whether increment/decrement buttons are visible. */
    bool buttonsVisible() const;

    /**
     * @brief Sets the minimum editable-field width in pixels.
     *
     * Values below zero are clamped to zero. A zero minimum lets the widget
     * use its built-in default minimum.
     */
    void setMinimumInputWidth(float width);

    /** @brief Returns the configured minimum editable-field width. */
    float minimumInputWidth() const;

    /** @brief Sets the preferred editable-field width in pixels. */
    void setPreferredInputWidth(float width);

    /** @brief Returns the configured preferred editable-field width. */
    float preferredInputWidth() const;

    /** @brief Sets the current value (clamped to [minimum, maximum]). */
    void setValue(int value);

    /** @brief Returns the current value. */
    int value() const;

    /**
     * @brief Sets minimum and maximum simultaneously.
     *
     * If `minimum > maximum` the endpoints are swapped automatically.
     * The current value is re-clamped to the new range.
     */
    void setRange(int minimum, int maximum);

    /** @brief Returns the minimum of the range. */
    int minimum() const;

    /** @brief Returns the maximum of the range. */
    int maximum() const;

    /**
     * @brief Sets the step used by the increment/decrement controls.
     *
     * A `step <= 0` is silently ignored; the previous step is kept.
     */
    void setStep(int step);

    /** @brief Returns the current step (always > 0). */
    int step() const;

    /**
     * @brief Emitted when the user changes the value and the effective value
     *        differs from the previous one.
     *
     * The argument is the new integer value.
     */
    Signal<int> valueChanged;

    Size sizeHint() const override;

protected:
    void renderImGui() override;
    void renderImGuiConstrained(float width, float height) override;

private:
    static constexpr std::size_t k_bufferCapacity = 64;

    void syncBuffer();
    void sanitizeBuffer();
    bool commitBufferValue(bool emitSignal, bool normalizeInvalid);
    bool applyStep(int direction);
    void renderWithAvailableWidth(float width);
    bool renderInput(float width);
    bool renderSideText(float width) const;
    float sideTextWidth() const;
    float buttonsWidth() const;
    float effectiveMinimumInputWidth() const;

    std::string m_label;
    std::vector<char> m_buffer;
    int         m_value   = 0;
    int         m_minimum = 0;
    int         m_maximum = 100;
    int         m_step    = 1;
    TextPlacement m_textPlacement = TextPlacement::Left;
    bool        m_buttonsVisible = true;
    float       m_minimumInputWidth = 0.0f;
    float       m_preferredInputWidth = 0.0f;
};

}  // namespace widgets
}  // namespace snf
