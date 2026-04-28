#include "SNFWidgets/ApplicationNode.h"
#include "SNFWidgets/Widget.h"

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>

// ImGui — internal to SNFWidgets, not part of the public API.
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

// ── Platform GL and GLFW includes ────────────────────────────────────────────
//
// Emscripten:
//   Define GLFW_INCLUDE_NONE so the contrib.glfw3 port does not try to
//   pull in its own OpenGL stub (which would collide with <GLES3/gl3.h>).
//   Include <GLES3/gl3.h> explicitly for the GL entry points we call.
//
// Desktop Linux:
//   GLFW_INCLUDE_NONE is NOT defined; <GLFW/glfw3.h> automatically includes
//   <GL/gl.h>, which provides the GL 1.x entry points we call directly
//   (glViewport, glClearColor, glClear).  Advanced GL functions are loaded
//   at runtime by ImGui's embedded opengl3 loader (imgui_impl_opengl3_loader.h).
#ifdef __EMSCRIPTEN__
#  define GLFW_INCLUDE_NONE
#  include <GLES3/gl3.h>
#  include <GLFW/emscripten_glfw3.h>
#  include <emscripten.h>
#  include <emscripten/html5.h>
#endif

#include <GLFW/glfw3.h>

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
    double cssWidth  = 0.0;
    double cssHeight = 0.0;
    emscripten_get_element_css_size("body", &cssWidth, &cssHeight);

    const int width  = static_cast<int>(cssWidth);
    const int height = static_cast<int>(cssHeight);
    if (width <= 0 || height <= 0) {
        return;
    }

    int currentWidth  = 0;
    int currentHeight = 0;
    glfwGetWindowSize(window, &currentWidth, &currentHeight);

    if (currentWidth != width || currentHeight != height) {
        glfwSetWindowSize(window, width, height);
        emscripten_set_canvas_element_size("#canvas", width, height);
    }
}
#endif  // __EMSCRIPTEN__

}  // namespace

// ── ApplicationNode ───────────────────────────────────────────────────────────

ApplicationNode::ApplicationNode(snf::Node* parent) : snf::Node(parent) {}

ApplicationNode::~ApplicationNode()
{
    destroyWindow();
}

void ApplicationNode::setTitle(const std::string& title)
{
    m_title = title;
    if (m_window) {
        glfwSetWindowTitle(m_window, m_title.c_str());
    }
}

void ApplicationNode::run()
{
    initWindow();
    initialized.emit();

#ifdef __EMSCRIPTEN__
    // Transfer control to the browser.  fps=0 → use requestAnimationFrame.
    // simulate_infinite_loop=1 → this call does not return.
    emscripten_set_main_loop_arg(mainLoopCallback, this, 0, 1);
#else
    // Desktop: blocking while-loop, exits when the window is closed.
    while (m_window && !glfwWindowShouldClose(m_window)) {
        tick();
    }
    shutdown.emit();
    destroyWindow();
#endif
}

GLFWwindow* ApplicationNode::window() const
{
    return m_window;
}

// ── private ───────────────────────────────────────────────────────────────────

void ApplicationNode::mainLoopCallback(void* userData)
{
    static_cast<ApplicationNode*>(userData)->tick();
}

void ApplicationNode::tick()
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
    //    ImGui has been composited, but before presentation.
    frame.emit();

    // 8. Swap the front and back buffers.  The contrib.glfw3 Emscripten port
    // presents the canvas as part of the browser frame; glfwSwapBuffers() is
    // intentionally not implemented there.
#ifndef __EMSCRIPTEN__
    glfwSwapBuffers(m_window);
#endif
}

void ApplicationNode::renderWidgets()
{
    for (std::size_t i = 0; i < childrenCount(); ++i) {
        if (auto* widget = dynamic_cast<Widget*>(getChild(i))) {
            widget->renderImGui();
        }
    }
}

void ApplicationNode::initWindow()
{
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "SNFWidgets: glfwInit failed\n");
        return;
    }

#ifdef __EMSCRIPTEN__
    // WebGL 2 via the Emscripten contrib.glfw3 port.  This port maps
    // GLFW_CONTEXT_VERSION_MAJOR directly to Emscripten's WebGL majorVersion,
    // so use 2 here even though the shader language is GLSL ES 3.00 below.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    emscripten_glfw_set_next_window_canvas_selector("#canvas");
#else
    // Desktop: OpenGL 3.3 Core Profile.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

    m_window = glfwCreateWindow(1280, 720, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::fprintf(stderr, "SNFWidgets: glfwCreateWindow failed\n");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);
#ifndef __EMSCRIPTEN__
    glfwSwapInterval(1);  // Enable vsync.
#endif

    // ── Dear ImGui context ────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Disable imgui.ini persistence everywhere: web has no filesystem,
    // and on desktop the default location (cwd) is rarely appropriate.
    ImGui::GetIO().IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);

#ifdef __EMSCRIPTEN__
    // Install wheel/pointer-lock callbacks required for correct scroll and
    // mouse behaviour inside the browser canvas.
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(m_window, "#canvas");

    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 330 core");
#endif

    m_imguiInitialised = true;
}

void ApplicationNode::destroyWindow()
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
