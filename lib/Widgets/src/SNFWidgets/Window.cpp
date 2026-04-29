#include "SNFWidgets/Window.h"

#include "SNFWidgets/Layout.h"

#include "imgui.h"  // internal to SNFWidgets, not part of the public API

#include <algorithm>

namespace snf {
namespace widgets {

namespace {
unsigned int flagValue(Window::Flag flag)
{
    return static_cast<unsigned int>(flag);
}

ImGuiWindowFlags toImGuiFlags(unsigned int flags)
{
    ImGuiWindowFlags imguiFlags = ImGuiWindowFlags_None;

    if ((flags & flagValue(Window::Flag::NoTitleBar)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoTitleBar;
    }
    if ((flags & flagValue(Window::Flag::NoResize)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoResize;
    }
    if ((flags & flagValue(Window::Flag::NoMove)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoMove;
    }
    if ((flags & flagValue(Window::Flag::NoCollapse)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoCollapse;
    }
    if ((flags & flagValue(Window::Flag::NoScrollbar)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoScrollbar;
    }
    if ((flags & flagValue(Window::Flag::NoScrollWithMouse)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoScrollWithMouse;
    }
    if ((flags & flagValue(Window::Flag::AlwaysAutoResize)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_AlwaysAutoResize;
    }
    if ((flags & flagValue(Window::Flag::NoSavedSettings)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoSavedSettings;
    }
    if ((flags & flagValue(Window::Flag::NoBackground)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_NoBackground;
    }
    if ((flags & flagValue(Window::Flag::MenuBar)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_MenuBar;
    }
    if ((flags & flagValue(Window::Flag::HorizontalScrollbar)) != 0u) {
        imguiFlags |= ImGuiWindowFlags_HorizontalScrollbar;
    }

    return imguiFlags;
}
}  // namespace

Window::Window(const std::string& title, snf::Node* parent)
    : Widget(parent), m_title(title)
{
}

void Window::setTitle(const std::string& title)
{
    m_title = title;
}

std::string Window::title() const
{
    return m_title;
}

void Window::setInitialSize(float width, float height)
{
    m_initialWidth = std::max(0.0f, width);
    m_initialHeight = std::max(0.0f, height);
    m_hasInitialSize = m_initialWidth > 0.0f && m_initialHeight > 0.0f;
}

void Window::clearInitialSize()
{
    m_hasInitialSize = false;
    m_initialWidth = 0.0f;
    m_initialHeight = 0.0f;
}

bool Window::hasInitialSize() const
{
    return m_hasInitialSize;
}

float Window::initialWidth() const
{
    return m_initialWidth;
}

float Window::initialHeight() const
{
    return m_initialHeight;
}

void Window::setInitialPosition(float x, float y)
{
    m_initialX = x;
    m_initialY = y;
    m_hasInitialPosition = true;
}

void Window::clearInitialPosition()
{
    m_hasInitialPosition = false;
    m_initialX = 0.0f;
    m_initialY = 0.0f;
}

bool Window::hasInitialPosition() const
{
    return m_hasInitialPosition;
}

float Window::initialX() const
{
    return m_initialX;
}

float Window::initialY() const
{
    return m_initialY;
}

void Window::setFullSize(bool enabled)
{
    m_fullSize = enabled;
}

bool Window::isFullSize() const
{
    return m_fullSize;
}

void Window::setFullSizeHidesTitleBar(bool enabled)
{
    m_fullSizeHidesTitleBar = enabled;
}

bool Window::fullSizeHidesTitleBar() const
{
    return m_fullSizeHidesTitleBar;
}

void Window::setAlwaysOnTop(bool enabled)
{
    m_alwaysOnTop = enabled;
}

bool Window::isAlwaysOnTop() const
{
    return m_alwaysOnTop;
}

void Window::setCollapsible(bool enabled)
{
    setFlag(Flag::NoCollapse, ! enabled);
}

bool Window::isCollapsible() const
{
    return ! testFlag(Flag::NoCollapse);
}

void Window::setResizable(bool enabled)
{
    setFlag(Flag::NoResize, ! enabled);
}

bool Window::isResizable() const
{
    return ! testFlag(Flag::NoResize);
}

void Window::setMovable(bool enabled)
{
    setFlag(Flag::NoMove, ! enabled);
}

bool Window::isMovable() const
{
    return ! testFlag(Flag::NoMove);
}

void Window::setTitleBarVisible(bool visible)
{
    setFlag(Flag::NoTitleBar, ! visible);
}

bool Window::isTitleBarVisible() const
{
    return ! testFlag(Flag::NoTitleBar);
}

void Window::setCloseButtonVisible(bool visible)
{
    m_closeButtonVisible = visible;
}

bool Window::isCloseButtonVisible() const
{
    return m_closeButtonVisible;
}

void Window::setScrollEnabled(bool enabled)
{
    setFlag(Flag::NoScrollbar, ! enabled);
    setFlag(Flag::NoScrollWithMouse, ! enabled);
}

bool Window::isScrollEnabled() const
{
    return ! testFlag(Flag::NoScrollbar) && ! testFlag(Flag::NoScrollWithMouse);
}

void Window::setFlag(Flag flag, bool enabled)
{
    if (enabled) {
        m_flags |= flagValue(flag);
    } else {
        m_flags &= ~flagValue(flag);
    }
}

bool Window::testFlag(Flag flag) const
{
    return (m_flags & flagValue(flag)) != 0u;
}

void Window::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    if (m_open && m_mode != Mode::Normal) {
        m_popupOpenRequested = true;
    }
}

Window::Mode Window::mode() const
{
    return m_mode;
}

void Window::open()
{
    m_open = true;
    if (m_mode != Mode::Normal) {
        m_popupOpenRequested = true;
    }
}

void Window::close()
{
    setOpenInternal(false, true);
}

bool Window::isOpen() const
{
    return m_open;
}

void Window::setLayout(Layout* layout)
{
    m_layout = layout;
    if (m_layout && m_layout->parent() != this) {
        m_layout->setParent(this);
    }
}

Layout* Window::layout() const
{
    return m_layout;
}

void Window::setOpenInternal(bool open, bool emitClosed)
{
    if (m_open == open) {
        return;
    }

    m_open = open;
    if (m_open && m_mode != Mode::Normal) {
        m_popupOpenRequested = true;
    }
    if (! m_open && emitClosed) {
        closed.emit();
    }
}

void Window::renderContents()
{
    if (! m_layout) {
        renderChildren();
        return;
    }

    for (std::size_t i = 0; i < childrenCount(); ++i) {
        auto* child = dynamic_cast<Widget*>(getChild(i));
        if (! child) {
            continue;
        }

        if (child == m_layout) {
            m_layout->Widget::renderWidget();
            continue;
        }

        if (! m_layout->containsWidget(child)) {
            child->renderWidget();
        }
    }
}

void Window::renderImGui()
{
    if (! m_open) {
        return;
    }

    unsigned int effectiveFlags = m_flags;
    const bool effectivelyEnabled = isEffectivelyEnabled();

    if (m_fullSize) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
        effectiveFlags |= flagValue(Flag::NoResize);
        effectiveFlags |= flagValue(Flag::NoMove);
        effectiveFlags |= flagValue(Flag::NoSavedSettings);
        if (m_fullSizeHidesTitleBar) {
            effectiveFlags |= flagValue(Flag::NoTitleBar);
        }
    } else {
        if (m_hasInitialSize) {
            ImGui::SetNextWindowSize(ImVec2(m_initialWidth, m_initialHeight), ImGuiCond_FirstUseEver);
        }
        if (m_hasInitialPosition) {
            ImGui::SetNextWindowPos(ImVec2(m_initialX, m_initialY), ImGuiCond_FirstUseEver);
        }
    }

    if (! effectivelyEnabled) {
        effectiveFlags |= flagValue(Flag::NoResize);
        effectiveFlags |= flagValue(Flag::NoMove);
        effectiveFlags |= flagValue(Flag::NoCollapse);
        effectiveFlags |= flagValue(Flag::NoScrollWithMouse);
    }

    if (m_alwaysOnTop) {
        ImGui::SetNextWindowFocus();
    }

    const ImGuiWindowFlags imguiFlags = toImGuiFlags(effectiveFlags);

    if (m_mode == Mode::Normal) {
        bool open = m_open;
        bool* openPtr = (effectivelyEnabled && m_closeButtonVisible) ? &open : nullptr;

        // ImGui::Begin() / ImGui::End() must always be paired, even when the
        // window is collapsed or clipped.  Children are rendered only when the
        // window body is visible (Begin returns true).
        if (ImGui::Begin(m_title.c_str(), openPtr, imguiFlags)) {
            renderContents();
        }
        ImGui::End();

        if (! open) {
            setOpenInternal(false, true);
        }
        return;
    }

    if (m_popupOpenRequested) {
        ImGui::OpenPopup(m_title.c_str());
        m_popupOpenRequested = false;
    }

    bool open = m_open;
    bool visible = false;
    if (m_mode == Mode::Modal) {
        bool* openPtr = (effectivelyEnabled && m_closeButtonVisible) ? &open : nullptr;
        visible = ImGui::BeginPopupModal(m_title.c_str(), openPtr, imguiFlags);
    } else {
        visible = ImGui::BeginPopup(m_title.c_str(), imguiFlags);
    }

    if (visible) {
        renderContents();
        ImGui::EndPopup();
    } else if (! m_popupOpenRequested && ! ImGui::IsPopupOpen(m_title.c_str())) {
        setOpenInternal(false, true);
    }

    if (! open) {
        setOpenInternal(false, true);
    }
}

}  // namespace widgets
}  // namespace snf
