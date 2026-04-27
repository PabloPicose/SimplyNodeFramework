#include "SNFWidgets/WebApplicationNode.h"

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>

#include <GLES3/gl3.h>

#include <cstdio>

namespace snf {
namespace widgets {

namespace {

void glfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

#ifdef __EMSCRIPTEN__
void resizeToBrowser(GLFWwindow* window)
{
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    emscripten_get_element_css_size("body", &cssWidth, &cssHeight);

    const int width = static_cast<int>(cssWidth);
    const int height = static_cast<int>(cssHeight);
    if (width <= 0 || height <= 0) {
        return;
    }

    int currentWidth = 0;
    int currentHeight = 0;
    glfwGetWindowSize(window, &currentWidth, &currentHeight);

    if (currentWidth != width || currentHeight != height) {
        glfwSetWindowSize(window, width, height);
        emscripten_set_canvas_element_size("#canvas", width, height);
    }
}
#endif  // __EMSCRIPTEN__

}  // namespace

// ── WebApplicationNode ────────────────────────────────────────────────────────

WebApplicationNode::WebApplicationNode(snf::Node* parent) : snf::Node(parent) {}

WebApplicationNode::~WebApplicationNode()
{
    destroyWindow();
}

void WebApplicationNode::run()
{
    initWindow();
    initialized.emit();

#ifdef __EMSCRIPTEN__
    // Transfer control to the browser; mainLoopCallback is called each frame.
    // The second argument (fps = 0) lets the browser choose the frame rate
    // (requestAnimationFrame).  The last argument (simulate_infinite_loop = 1)
    // means emscripten_set_main_loop_arg does not return.
    emscripten_set_main_loop_arg(mainLoopCallback, this, 0, 1);
#else
    // Native fallback: useful for local development without a browser.
    while (m_window && !glfwWindowShouldClose(m_window)) {
        tick();
    }

    shutdown.emit();
    destroyWindow();
#endif
}

GLFWwindow* WebApplicationNode::window() const
{
    return m_window;
}

// ── private ───────────────────────────────────────────────────────────────────

// update() is a no-op for WebApplicationNode: the node receives per-frame
// work through the Emscripten main-loop callback (tick()), not via the
// EventLoop root-node update mechanism.

void WebApplicationNode::mainLoopCallback(void* userData)
{
    static_cast<WebApplicationNode*>(userData)->tick();
}

void WebApplicationNode::tick()
{
#ifdef __EMSCRIPTEN__
    resizeToBrowser(m_window);
#endif

    glfwPollEvents();

    // Process all SNF tasks, due timers, and signals for this frame.
    // runPendingWork() is used instead of Application::run() because:
    //   - run() blocks until there is no more work.
    //   - With the Emscripten stub, wait() returns immediately, so run()
    //     would spin-poll when active timers are present.
    //   - runPendingWork() does one non-blocking pass and returns, which
    //     is the correct behaviour for a per-frame browser callback.
    if (snf::Application* appInst = snf::Application::instance()) {
        if (snf::EventLoop* loop = appInst->getOrCreateCurrentThreadEventLoop()) {
            loop->runPendingWork();
        }
    }

    frame.emit();

    glfwSwapBuffers(m_window);
}

void WebApplicationNode::initWindow()
{
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "SNFWidgets: glfwInit failed\n");
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    m_window = glfwCreateWindow(1280, 720, "SNFWidgets", nullptr, nullptr);

    if (!m_window) {
        std::fprintf(stderr, "SNFWidgets: glfwCreateWindow failed\n");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
}

void WebApplicationNode::destroyWindow()
{
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }
}

}  // namespace widgets
}  // namespace snf
