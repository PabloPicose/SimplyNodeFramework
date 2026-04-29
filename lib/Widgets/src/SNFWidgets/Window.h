#pragma once

/**
 * @file Window.h
 * @brief ImGui-backed window widget.
 * @ingroup SNFWidgets
 */

#include "Widget.h"

#include <SNFCore/Connection.h>

#include <string>

namespace snf {
namespace widgets {

class Layout;

/**
 * @class Window
 * @ingroup SNFWidgets
 * @brief A resizable, movable Dear ImGui window.
 *
 * Add any `Widget` as a child to populate the window's contents:
 *
 * @code
 * snf::widgets::Window     win("Settings", &webApp);
 * snf::widgets::PushButton btn("Apply",    &win);
 * @endcode
 *
 * The window title can be changed at any time via `setTitle()`.
 */
class Window : public Widget
{
public:
    /**
     * @enum Mode
     * @brief Rendering mode for the window.
     *
     * `Modal` is implemented with ImGui modal popups, so interaction behind
     * the dialog is blocked while the modal is open. `Popup` uses regular
     * ImGui popups. `Normal` is the default window behaviour.
     */
    enum class Mode {
        Normal,
        Popup,
        Modal,
    };

    /**
     * @enum Flag
     * @brief SNFWidgets window flags translated internally to ImGui flags.
     */
    enum class Flag : unsigned int {
        NoTitleBar = 1u << 0,
        NoResize = 1u << 1,
        NoMove = 1u << 2,
        NoCollapse = 1u << 3,
        NoScrollbar = 1u << 4,
        NoScrollWithMouse = 1u << 5,
        AlwaysAutoResize = 1u << 6,
        NoSavedSettings = 1u << 7,
        NoBackground = 1u << 8,
        MenuBar = 1u << 9,
        HorizontalScrollbar = 1u << 10,
    };

    /**
     * @param title  Window title shown in the title bar.
     * @param parent Parent node (another Widget or an ApplicationNode).
     */
    explicit Window(const std::string& title, snf::Node* parent = nullptr);

    /** @brief Changes the window title. */
    void setTitle(const std::string& title);

    /** @brief Returns the current window title. */
    std::string title() const;

    /** @brief Sets the initial size used by ImGui on first use. */
    void setInitialSize(float width, float height);

    /** @brief Clears the configured initial size. */
    void clearInitialSize();

    /** @brief Returns whether an initial size is configured. */
    bool hasInitialSize() const;

    /** @brief Returns the configured initial width, or zero. */
    float initialWidth() const;

    /** @brief Returns the configured initial height, or zero. */
    float initialHeight() const;

    /** @brief Sets the initial position used by ImGui on first use. */
    void setInitialPosition(float x, float y);

    /** @brief Clears the configured initial position. */
    void clearInitialPosition();

    /** @brief Returns whether an initial position is configured. */
    bool hasInitialPosition() const;

    /** @brief Returns the configured initial x coordinate, or zero. */
    float initialX() const;

    /** @brief Returns the configured initial y coordinate, or zero. */
    float initialY() const;

    /**
     * @brief Enables a full-size root/panel mode.
     *
     * In this mode the window is placed at the current viewport work origin
     * and sized to the viewport work area every frame. Resize and move are
     * disabled for that frame. The title bar is hidden by default and can be
     * controlled with `setFullSizeHidesTitleBar()`.
     */
    void setFullSize(bool enabled);

    /** @brief Returns whether full-size mode is enabled. */
    bool isFullSize() const;

    /** @brief Controls whether full-size mode hides the title bar. */
    void setFullSizeHidesTitleBar(bool enabled);

    /** @brief Returns whether full-size mode hides the title bar. */
    bool fullSizeHidesTitleBar() const;

    /**
     * @brief Requests rendering above regular windows when possible.
     *
     * Dear ImGui does not expose native always-on-top window semantics. SNF
     * resolves this by focusing the window while rendering; for strict modal
     * blocking use `Mode::Modal`.
     */
    void setAlwaysOnTop(bool enabled);

    /** @brief Returns whether always-on-top behaviour is requested. */
    bool isAlwaysOnTop() const;

    /** @brief Enables or disables collapsing via the title bar. */
    void setCollapsible(bool enabled);

    /** @brief Returns whether the window can be collapsed. */
    bool isCollapsible() const;

    /** @brief Enables or disables resizing. */
    void setResizable(bool enabled);

    /** @brief Returns whether the window can be resized. */
    bool isResizable() const;

    /** @brief Enables or disables moving. */
    void setMovable(bool enabled);

    /** @brief Returns whether the window can be moved. */
    bool isMovable() const;

    /** @brief Shows or hides the title bar. */
    void setTitleBarVisible(bool visible);

    /** @brief Returns whether the title bar is visible. */
    bool isTitleBarVisible() const;

    /** @brief Shows or hides the ImGui close button. */
    void setCloseButtonVisible(bool visible);

    /** @brief Returns whether the close button is visible. */
    bool isCloseButtonVisible() const;

    /** @brief Enables or disables scrollbars and mouse-wheel scrolling. */
    void setScrollEnabled(bool enabled);

    /** @brief Returns whether scrolling is enabled. */
    bool isScrollEnabled() const;

    /** @brief Sets or clears a SNFWidgets window flag. */
    void setFlag(Flag flag, bool enabled);

    /** @brief Returns whether a SNFWidgets window flag is set. */
    bool testFlag(Flag flag) const;

    /** @brief Sets popup/modal/normal rendering mode. */
    void setMode(Mode mode);

    /** @brief Returns the current rendering mode. */
    Mode mode() const;

    /** @brief Opens the window or popup/modal. */
    void open();

    /** @brief Closes the window or popup/modal from code. */
    void close();

    /** @brief Returns whether the window is logically open. */
    bool isOpen() const;

    /**
     * @brief Installs a layout as the window's content manager.
     *
     * The layout is reparented to the window when needed. Passing nullptr
     * disables layout mode and the window renders its direct widget children
     * in insertion order as before.
     */
    void setLayout(Layout* layout);

    /** @brief Returns the currently installed layout, or nullptr. */
    Layout* layout() const;

    /** @brief Emitted when the window transitions from open to closed. */
    Signal<> closed;

protected:
    void renderImGui() override;

private:
    void renderContents();
    void setOpenInternal(bool open, bool emitClosed);

    std::string m_title;
    Layout*     m_layout = nullptr;
    unsigned int m_flags = 0;
    Mode        m_mode = Mode::Normal;
    bool        m_open = true;
    bool        m_popupOpenRequested = false;
    bool        m_hasInitialSize = false;
    bool        m_hasInitialPosition = false;
    bool        m_fullSize = false;
    bool        m_fullSizeHidesTitleBar = true;
    bool        m_alwaysOnTop = false;
    bool        m_closeButtonVisible = false;
    float       m_initialWidth = 0.0f;
    float       m_initialHeight = 0.0f;
    float       m_initialX = 0.0f;
    float       m_initialY = 0.0f;
};

}  // namespace widgets
}  // namespace snf
