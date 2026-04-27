#pragma once

/**
 * @file WebApplicationNode.h
 * @brief Emscripten+GLFW integration node for WebAssembly builds.
 * @ingroup SNFWidgets
 *
 * @note This class is only available when building with SNF_WEB_ASSEMBLY=ON.
 *       It requires the Emscripten toolchain and uses the contrib.glfw3 port.
 */

#include <SNFCore/Node.h>
#include <SNFCore/Connection.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <GLFW/glfw3.h>

namespace snf {
namespace widgets {

/**
 * @class WebApplicationNode
 * @ingroup SNFWidgets
 * @brief Drives a GLFW window and the SNFCore event loop from the browser's
 *        animation-frame callback.
 *
 * Create one instance alongside `snf::Application` and call `run()`.
 * The browser's animation-frame loop invokes `tick()` on every frame, which:
 *   - synchronises the canvas element size with the browser window,
 *   - calls `EventLoop::runPendingWork()` to process pending SNF tasks,
 *     timers, and signals, and
 *   - emits the `frame` signal so that the application can render.
 *
 * Typical usage:
 * @code
 * int main() {
 *     snf::Application app(0, nullptr);
 *
 *     snf::Timer ticker;
 *     ticker.timeout.connect([]() { printf("tick\n"); });
 *     ticker.start(std::chrono::seconds(1));
 *
 *     snf::widgets::WebApplicationNode webApp;
 *     webApp.frame.connect([&]() {
 *         // render your ImGui / OpenGL frame here
 *     });
 *
 *     webApp.run();   // does not return on Emscripten
 *     return 0;
 * }
 * @endcode
 *
 * @note `SNFCore` is UI-free.  All rendering code belongs in `frame` signal
 *       handlers, not in SNFCore itself.
 */
class WebApplicationNode : public snf::Node
{
public:
    /**
     * @brief Constructs the web application node.
     * @param parent Optional parent node.  Pass `nullptr` (default) to create
     *               a root node owned by the main-thread EventLoop.
     */
    explicit WebApplicationNode(snf::Node* parent = nullptr);

    ~WebApplicationNode() override;

    /**
     * @brief Starts the window and enters the main loop.
     *
     * On Emscripten this calls `emscripten_set_main_loop_arg()` and does
     * **not** return.  On a native build (useful for local development) it
     * falls back to a standard GLFW while-loop that exits when the window
     * is closed.
     */
    void run();

    /** @brief Returns the GLFW window handle; `nullptr` before `run()`. */
    GLFWwindow* window() const;

    /** @brief Per-iteration update hook from SNFCore's node system (no-op here). */
    void update() override {}

    // ── Signals ──────────────────────────────────────────────────────────────

    /** @brief Emitted once after the GLFW window and OpenGL ES context are
     *         ready. Connect rendering initialisation here. */
    Signal<> initialized;

    /** @brief Emitted every frame just before the GL swap.  Connect all
     *         per-frame rendering code here. */
    Signal<> frame;

    /** @brief Emitted just before the GLFW window is destroyed. */
    Signal<> shutdown;

private:
    static void mainLoopCallback(void* userData);
    void tick();
    void initWindow();
    void destroyWindow();

    GLFWwindow* m_window = nullptr;
};

}  // namespace widgets
}  // namespace snf
