#include "SNFWidgets/Slider.h"

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

Slider::Slider(const std::string& label, int minimum, int maximum, snf::Node* parent)
    : Widget(parent), m_label(label), m_minimum(minimum), m_maximum(maximum)
{
    normalizeRange(m_minimum, m_maximum);
    m_value = m_minimum;
}

void Slider::setLabel(const std::string& label)
{
    m_label = label;
}

std::string Slider::label() const
{
    return m_label;
}

void Slider::setValue(int value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
}

int Slider::value() const
{
    return m_value;
}

void Slider::setRange(int minimum, int maximum)
{
    normalizeRange(minimum, maximum);
    m_minimum = minimum;
    m_maximum = maximum;
    m_value   = std::clamp(m_value, m_minimum, m_maximum);
}

int Slider::minimum() const
{
    return m_minimum;
}

int Slider::maximum() const
{
    return m_maximum;
}

void Slider::renderImGui()
{
    const int prev = m_value;
    if (ImGui::SliderInt(m_label.c_str(), &m_value, m_minimum, m_maximum)) {
        if (m_value != prev) {
            valueChanged.emit(m_value);
        }
    }
}

}  // namespace widgets
}  // namespace snf
