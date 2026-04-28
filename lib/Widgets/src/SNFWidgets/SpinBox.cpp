#include "SNFWidgets/SpinBox.h"

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

SpinBox::SpinBox(const std::string& label, int minimum, int maximum, snf::Node* parent)
    : Widget(parent), m_label(label), m_minimum(minimum), m_maximum(maximum)
{
    normalizeRange(m_minimum, m_maximum);
    m_value = m_minimum;
}

void SpinBox::setLabel(const std::string& label)
{
    m_label = label;
}

std::string SpinBox::label() const
{
    return m_label;
}

void SpinBox::setValue(int value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
}

int SpinBox::value() const
{
    return m_value;
}

void SpinBox::setRange(int minimum, int maximum)
{
    normalizeRange(minimum, maximum);
    m_minimum = minimum;
    m_maximum = maximum;
    m_value   = std::clamp(m_value, m_minimum, m_maximum);
}

int SpinBox::minimum() const
{
    return m_minimum;
}

int SpinBox::maximum() const
{
    return m_maximum;
}

void SpinBox::setStep(int step)
{
    if (step > 0) {
        m_step = step;
    }
    // step <= 0 is silently ignored; m_step remains unchanged.
}

int SpinBox::step() const
{
    return m_step;
}

void SpinBox::renderImGui()
{
    const int prev = m_value;
    if (ImGui::InputInt(m_label.c_str(), &m_value, m_step, m_step * 10)) {
        m_value = std::clamp(m_value, m_minimum, m_maximum);
        if (m_value != prev) {
            valueChanged.emit(m_value);
        }
    }
}

}  // namespace widgets
}  // namespace snf
