#pragma once

/**
 * @file ProgressBar.h
 * @brief Visual progress indicator widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <string>

namespace snf {
namespace widgets {

/**
 * @class ProgressBar
 * @ingroup SNFWidgets
 * @brief A read-only progress bar with an integer value range.
 *
 * The bar computes the fill fraction internally from `[minimum, maximum]`
 * and the current `value`.  If `minimum == maximum` the bar renders as
 * fully filled.  If the range is inverted (`minimum > maximum`) it is
 * normalised by swapping the endpoints.
 *
 * An optional overlay text (e.g. "75 %") can be shown inside the bar.
 * Pass an empty string to disable it.
 *
 * ProgressBar has no signals because it is a display-only widget.
 *
 * @code
 * snf::widgets::ProgressBar bar(0, 100, &win);
 * bar.setValue(42);
 * bar.setOverlayText("42 %");
 * @endcode
 */
class ProgressBar : public Widget
{
public:
    /**
     * @param minimum Lower bound of the range (inclusive).
     * @param maximum Upper bound of the range (inclusive).
     * @param parent  Parent node (a Window or another container Widget).
     *
     * If `minimum > maximum` the endpoints are swapped automatically.
     */
    explicit ProgressBar(int minimum = 0, int maximum = 100, snf::Node* parent = nullptr);

    /** @brief Sets the current value (clamped to [minimum, maximum]). */
    void setValue(int value);

    /** @brief Returns the current value. */
    int value() const;

    /** @brief Sets minimum and maximum simultaneously.
     *
     *  If `minimum > maximum` the endpoints are swapped automatically.
     *  The current value is re-clamped to the new range.
     */
    void setRange(int minimum, int maximum);

    /** @brief Returns the minimum of the range. */
    int minimum() const;

    /** @brief Returns the maximum of the range. */
    int maximum() const;

    /**
     * @brief Sets the optional overlay text drawn on top of the bar.
     *
     * Pass an empty string to hide the text (default behaviour).
     */
    void setOverlayText(const std::string& text);

    /** @brief Returns the current overlay text. */
    std::string overlayText() const;

    Size sizeHint() const override;

protected:
    void renderImGui() override;

private:
    int         m_value   = 0;
    int         m_minimum = 0;
    int         m_maximum = 100;
    std::string m_overlayText;
};

}  // namespace widgets
}  // namespace snf
