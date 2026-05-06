#pragma once

/**
 * @file ApplicationNode.h
 * @brief GLFW+ImGui application root node for both WebAssembly and desktop.
 * @ingroup SNFWidgets
 */

#include <SNFCore/Node.h>
#include <SNFCore/Connection.h>

#include <string>

// Forward-declare the opaque GLFW type to keep this header free of GLFW and
// Emscripten includes.  Consumers never need to include GLFW headers just to
// hold an ApplicationNode.
struct GLFWwindow;

namespace snf {
namespace widgets {

/**
 * @class ApplicationNode
 * @ingroup SNFWidgets
 * @brief Root node that drives a GLFW window, a Dear ImGui context, and the
 *        SNFCore event loop.
 *
 * Works transparently on two platforms — no application-level `#ifdef` needed:
 *
 *  - **WebAssembly** (`SNF_WEB_ASSEMBLY=ON` / Emscripten): uses the
 *    `contrib.glfw3` Emscripten port, OpenGL ES 3, and the browser
 *    `requestAnimationFrame` callback as the main loop.
 *  - **Desktop Linux** (`SNF_WEB_ASSEMBLY=OFF`): uses GLFW 3.4+,
 *    OpenGL 3.3 Core Profile, and a regular blocking while-loop.
 *
 * Every frame the node automatically:
 *  1. Synchronises the canvas to the browser window (web only).
 *  2. Polls GLFW input events.
 *  3. Runs one non-blocking SNFCore event-loop pass (tasks, timers, signals).
 *  4. Starts a Dear ImGui frame.
 *  5. Renders all child @ref Widget nodes recursively.
 *  6. Finalises and presents the ImGui draw data.
 *  7. Emits @ref frame (raw-GL post-render hook).
 *  8. Presents the frame (desktop swaps the GLFW back-buffer; web presents
 *     through the browser frame callback).
 *
 * Dear ImGui is managed entirely by this class.  Application code never
 * includes `imgui.h` — just instantiate widgets and connect signals:
 *
 * @code
 * snf::Application app(0, nullptr);
 *
 * snf::widgets::ApplicationNode appNode;
 * appNode.setTitle("My App");
 *
 * snf::widgets::Window     win("Settings", &appNode);
 * snf::widgets::PushButton btn("Apply",    &win);
 *
 * btn.clicked.connect([]() { std::printf("Apply!\n"); });
 *
 * // Does not return on Emscripten; blocks until window close on desktop.
 * return app.run();
 * @endcode
 */
class ApplicationNode : public snf::Node
{
public:
    explicit ApplicationNode(snf::Node* parent = nullptr);
    ~ApplicationNode() override;

    /**
     * @brief Sets the GLFW window title.
     *
     * If called before `run()` the title is applied when the window is
     * created.  If called after, it is applied immediately.
     */
    void setTitle(const std::string& title);

    /**
     * @brief Starts the window and enters the main loop.
     *
     * Prefer calling `snf::Application::run()` from application code. This
     * method remains available for lower-level integrations and backwards
     * compatibility.
     *
     * On Emscripten this calls `emscripten_set_main_loop_arg()` and does
     * **not** return.  On desktop it runs a blocking GLFW while-loop that
     * exits when the window is closed, then emits @ref shutdown and cleans up.
     */
    void run();

    /** @brief Returns the raw GLFW window handle; `nullptr` before `run()`. */
    GLFWwindow* window() const;

    /** @brief Per-iteration SNFCore update hook (no-op; frame work is in tick()). */
    void update() override {}

    // ── Signals ──────────────────────────────────────────────────────────────

    /**
     * @brief Emitted once after the GLFW window, OpenGL context, and Dear
     *        ImGui context are ready.
     *
     * Use this to perform one-time GL resource initialisation (load textures,
     * compile shaders, etc.).  ImGui calls are valid at this point.
     */
    Signal<> initialized;

    /**
     * @brief Emitted every frame after ImGui content has been fully rendered
     *        and drawn to the GL framebuffer, just before presentation.
     *
     * Use this for raw OpenGL operations (custom GL drawing, post-processing).
     * ImGui draw calls are NOT valid inside this handler.
     */
    Signal<> frame;

    /**
     * @brief Emitted just before the ImGui context and GLFW window are
     *        destroyed.
     *
     * @note On desktop this is emitted at the end of `run()` when the window
     *       is closed.  On Emscripten, browsers close the tab directly and
     *       this signal is typically not reached.
     */
    Signal<> shutdown;

private:
    static void mainLoopCallback(void* userData);
    void tick();
    void initWindow();
    void destroyWindow();
    void renderWidgets();

    GLFWwindow* m_window           = nullptr;
    bool        m_imguiInitialised = false;
    std::string m_title            = "SNFWidgets";
};

}  // namespace widgets
}  // namespace snf
