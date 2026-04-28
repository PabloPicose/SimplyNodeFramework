#pragma once

/**
 * @file SpinBox.h
 * @brief Integer spin box widget with increment/decrement controls.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>

namespace snf {
namespace widgets {

/**
 * @class SpinBox
 * @ingroup SNFWidgets
 * @brief A labelled numeric input with up/down step controls (integers only).
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
    /**
     * @param label   Text shown next to the spin box.
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

protected:
    void renderImGui() override;

private:
    std::string m_label;
    int         m_value   = 0;
    int         m_minimum = 0;
    int         m_maximum = 100;
    int         m_step    = 1;
};

}  // namespace widgets
}  // namespace snf
