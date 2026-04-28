#pragma once

/**
 * @file Slider.h
 * @brief Integer drag-slider widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"
#include <SNFCore/Connection.h>

#include <string>

namespace snf {
namespace widgets {

/**
 * @class Slider
 * @ingroup SNFWidgets
 * @brief A labelled horizontal slider with an integer value range.
 *
 * `valueChanged` is emitted (with the new integer value) only when the
 * effective value changes due to user interaction.  Programmatic setters do
 * not emit signals, and there are no duplicate emissions within a frame when
 * the value is unchanged.
 *
 * If `minimum > maximum` the endpoints are swapped automatically; values
 * outside the range are clamped.
 *
 * @code
 * snf::widgets::Slider sld("Volume", 0, 100, &win);
 * sld.setValue(50);
 *
 * sld.valueChanged.connect([](int v) {
 *     std::printf("Volume: %d\n", v);
 * });
 * @endcode
 */
class Slider : public Widget
{
public:
    /**
     * @param label   Text shown next to the slider.
     * @param minimum Lower bound (inclusive).
     * @param maximum Upper bound (inclusive).
     * @param parent  Parent node (a Window or another container Widget).
     *
     * If `minimum > maximum` the endpoints are swapped automatically.
     */
    explicit Slider(const std::string& label,
                    int minimum = 0,
                    int maximum = 100,
                    snf::Node* parent = nullptr);

    /** @brief Sets the slider label. */
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
     * @brief Emitted when the user drags the slider and the effective value
     *        changes.
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
};

}  // namespace widgets
}  // namespace snf
