#include "SNFWidgets/WebApplicationNode.h"
#include "SNFWidgets/Widget.h"

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>

// ImGui — internal to SNFWidgets, not part of the public API.
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLES3/gl3.h>
#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

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

    const int width  = static_cast<int>(cssWidth);
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

void WebApplicationNode::setTitle(const std::string& title)
{
    m_title = title;
    if (m_window) {
        glfwSetWindowTitle(m_window, m_title.c_str());
    }
}

void WebApplicationNode::run()
{
    initWindow();
    initialized.emit();

#ifdef __EMSCRIPTEN__
    // Transfer control to the browser.  fps=0 → use requestAnimationFrame.
    // simulate_infinite_loop=1 → this call does not return.
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

void WebApplicationNode::mainLoopCallback(void* userData)
{
    static_cast<WebApplicationNode*>(userData)->tick();
}

void WebApplicationNode::tick()
{
#ifdef __EMSCRIPTEN__
    resizeToBrowser(m_window);
#endif

    // 1. Poll input events.
    glfwPollEvents();

    // 2. Run one non-blocking SNFCore event-loop pass: tasks, due timers,
    //    queued signals.  runPendingWork() is used instead of Application::run()
    //    because run() blocks until all work is exhausted and would spin-poll
    //    with the no-op Emscripten I/O stub.
    if (snf::Application* appInst = snf::Application::instance()) {
        if (snf::EventLoop* loop = appInst->getOrCreateCurrentThreadEventLoop()) {
            loop->runPendingWork();
        }
    }

    // 3. Start a new ImGui frame.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 4. Render the widget tree: all direct Widget children of this node,
    //    each of which recursively renders its own Widget children.
    renderWidgets();

    // 5. Finalise ImGui geometry.
    ImGui::Render();

    // 6. Clear the GL framebuffer and draw the ImGui draw data.
    int fbW = 0;
    int fbH = 0;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // 7. Post-render hook: raw OpenGL operations that should execute after
    //    ImGui has been composited (but before the buffer swap).
    frame.emit();

    // 8. Swap the front and back buffers.
    glfwSwapBuffers(m_window);
}

void WebApplicationNode::renderWidgets()
{
    for (std::size_t i = 0; i < childrenCount(); ++i) {
        if (auto* widget = dynamic_cast<Widget*>(getChild(i))) {
            widget->renderImGui();
        }
    }
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

    m_window = glfwCreateWindow(1280, 720, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::fprintf(stderr, "SNFWidgets: glfwCreateWindow failed\n");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    // ── Dear ImGui context ────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // Disable imgui.ini (no filesystem in web builds).

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);

#ifdef __EMSCRIPTEN__
    // Install wheel/pointer-lock callbacks required for correct scroll and
    // mouse behaviour inside the browser canvas.
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(m_window, "#canvas");
#endif

    ImGui_ImplOpenGL3_Init("#version 300 es");

    m_imguiInitialised = true;
}

void WebApplicationNode::destroyWindow()
{
    if (m_imguiInitialised) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_imguiInitialised = false;
    }

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }
}

}  // namespace widgets
}  // namespace snf
