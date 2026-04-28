#include "SNFWidgets/ProgressBar.h"

#include "imgui.h"

#include <algorithm>

namespace snf {
namespace widgets {

namespace {
void normalizeRange(int& minimum, int& maximum)
{
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
}
}  // namespace

ProgressBar::ProgressBar(int minimum, int maximum, snf::Node* parent)
    : Widget(parent), m_minimum(minimum), m_maximum(maximum)
{
    normalizeRange(m_minimum, m_maximum);
    m_value = m_minimum;
}

void ProgressBar::setValue(int value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
}

int ProgressBar::value() const
{
    return m_value;
}

void ProgressBar::setRange(int minimum, int maximum)
{
    normalizeRange(minimum, maximum);
    m_minimum = minimum;
    m_maximum = maximum;
    m_value   = std::clamp(m_value, m_minimum, m_maximum);
}

int ProgressBar::minimum() const
{
    return m_minimum;
}

int ProgressBar::maximum() const
{
    return m_maximum;
}

void ProgressBar::setOverlayText(const std::string& text)
{
    m_overlayText = text;
}

std::string ProgressBar::overlayText() const
{
    return m_overlayText;
}

void ProgressBar::renderImGui()
{
    // Protect against minimum == maximum: treat as fully filled.
    const float fraction = (m_minimum == m_maximum)
        ? 1.0f
        : static_cast<float>(m_value - m_minimum) /
          static_cast<float>(m_maximum - m_minimum);

    // Pass nullptr for overlay to let ImGui show the default percentage text;
    // pass the overlay string when the user has set one.
    const char* overlay = m_overlayText.empty() ? nullptr : m_overlayText.c_str();
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), overlay);
}

}  // namespace widgets
}  // namespace snf
