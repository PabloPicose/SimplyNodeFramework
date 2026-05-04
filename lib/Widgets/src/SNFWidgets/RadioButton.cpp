#include "SNFWidgets/RadioButton.h"

#include "imgui.h"

#include <algorithm>
#include <vector>

namespace snf {
namespace widgets {

namespace {
bool containsButton(const std::vector<RadioButton*>& buttons, const RadioButton* button)
{
    return std::find(buttons.begin(), buttons.end(), button) != buttons.end();
}
}  // namespace

RadioButton::RadioButton(const std::string& label, snf::Node* parent)
    : Widget(parent), m_label(label)
{
}

void RadioButton::setLabel(const std::string& label)
{
    m_label = label;
}

std::string RadioButton::label() const
{
    return m_label;
}

void RadioButton::setSelected(bool selected)
{
    setSelectedInternal(selected, false);
}

bool RadioButton::isSelected() const
{
    return m_selected;
}

void RadioButton::link(RadioButton* other)
{
    if (! other || other == this) {
        return;
    }

    std::vector<RadioButton*> group;
    group.push_back(this);

    for (auto* peer : linkedPeers()) {
        if (! containsButton(group, peer)) {
            group.push_back(peer);
        }
    }

    if (! containsButton(group, other)) {
        group.push_back(other);
    }

    for (auto* peer : other->linkedPeers()) {
        if (! containsButton(group, peer)) {
            group.push_back(peer);
        }
    }

    for (auto* lhs : group) {
        for (auto* rhs : group) {
            if (lhs != rhs) {
                lhs->addLinkedPeer(rhs);
            }
        }
    }

    RadioButton* selectedButton = nullptr;
    for (auto* button : group) {
        if (button->isSelected()) {
            if (! selectedButton) {
                selectedButton = button;
            } else {
                button->m_selected = false;
            }
        }
    }
}

void RadioButton::linkAll(std::initializer_list<RadioButton*> buttons)
{
    RadioButton* first = nullptr;
    for (auto* button : buttons) {
        if (! button) {
            continue;
        }

        if (! first) {
            first = button;
            continue;
        }

        first->link(button);
    }
}

bool RadioButton::isLinkedTo(const RadioButton* other) const
{
    if (! other || other == this) {
        return false;
    }

    for (const auto& peer : m_linkedButtons) {
        if (peer && ! peer.isMarkedToDelete() && peer.get() == other) {
            return true;
        }
    }

    return false;
}

int RadioButton::linkedButtonCount() const
{
    return static_cast<int>(linkedPeers().size());
}

Size RadioButton::sizeHint() const
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return {};
    }

    const float frameHeight = ImGui::GetFrameHeight();
    const ImVec2 textSize = ImGui::CalcTextSize(m_label.c_str(), nullptr, true);
    const float width = frameHeight
        + (textSize.x > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x + textSize.x : 0.0f);
    return Size{width, frameHeight};
}

void RadioButton::renderImGui()
{
    // ImGui::RadioButton(label, active) renders a single radio button that
    // returns true when clicked regardless of its previous state. We track
    // state ourselves to detect actual changes and emit only on real change.
    if (ImGui::RadioButton(m_label.c_str(), m_selected)) {
        if (!m_selected) {
            setSelectedInternal(true, true);
        }
    }
}

void RadioButton::setSelectedInternal(bool selected, bool emitSignals)
{
    if (selected) {
        for (auto* peer : linkedPeers()) {
            if (peer->m_selected) {
                peer->m_selected = false;
                if (emitSignals) {
                    peer->stateChanged.emit(false);
                }
            }
        }
    }

    if (m_selected == selected) {
        return;
    }

    m_selected = selected;
    if (emitSignals) {
        stateChanged.emit(m_selected);
    }
}

void RadioButton::addLinkedPeer(RadioButton* other)
{
    if (! other || other == this || isLinkedTo(other)) {
        return;
    }

    m_linkedButtons.emplace_back(other);
}

std::vector<RadioButton*> RadioButton::linkedPeers() const
{
    std::vector<RadioButton*> peers;
    peers.reserve(m_linkedButtons.size());

    for (const auto& peer : m_linkedButtons) {
        if (peer && ! peer.isMarkedToDelete()) {
            peers.push_back(peer.get());
        }
    }

    return peers;
}

}  // namespace widgets
}  // namespace snf
