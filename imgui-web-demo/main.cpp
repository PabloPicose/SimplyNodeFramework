#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <GLES3/gl3.h>
#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
static void resize_to_browser(GLFWwindow* window)
{
    double css_width = 0.0;
    double css_height = 0.0;

    emscripten_get_element_css_size("body", &css_width, &css_height);

    const int width = static_cast<int>(css_width);
    const int height = static_cast<int>(css_height);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    int current_width = 0;
    int current_height = 0;
    glfwGetWindowSize(window, &current_width, &current_height);

    if (current_width != width || current_height != height)
    {
        glfwSetWindowSize(window, width, height);
        emscripten_set_canvas_element_size("#canvas", width, height);
    }
}
#endif


struct AppState
{
    GLFWwindow* window = nullptr;
    bool show_demo_window = true;
    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
};

static void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void main_loop(void* user_data)
{
    auto* app = static_cast<AppState*>(user_data);

#ifdef __EMSCRIPTEN__
    resize_to_browser(app->window);
#endif

    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (app->show_demo_window)
    {
        ImGui::ShowDemoWindow(&app->show_demo_window);
    }

    ImGui::Render();

    int display_w = 0;
    int display_h = 0;
    glfwGetFramebufferSize(app->window, &display_w, &display_h);

    glViewport(0, 0, display_w, display_h);

    const ImVec4& color = app->clear_color;
    glClearColor(color.x, color.y, color.z, color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(app->window);
}

int main()
{
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit())
    {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 300 es";

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(
        1280,
        720,
        "Dear ImGui Web Demo",
        nullptr,
        nullptr
    );

    if (window == nullptr)
    {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    static AppState app;
    app.window = window;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &app, 0, true);
#else
    while (!glfwWindowShouldClose(window))
    {
        main_loop(&app);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
#endif

    return 0;
}
