#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFWidgets/WebApplicationNode.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLES3/gl3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

/**
 * SNFWidgets + ImGui demo.
 *
 * Demonstrates the full integration between:
 *   - snf::Application / snf::Timer  (SNFCore event loop running each frame)
 *   - snf::widgets::WebApplicationNode  (Emscripten+GLFW window + main loop)
 *   - Dear ImGui  (demo window rendered on every frame)
 *
 * The ImGui lifecycle is wired entirely through WebApplicationNode signals:
 *   initialized → ImGui context + backend init
 *   frame       → ImGui new-frame, demo window, render
 *   shutdown    → ImGui cleanup  (native builds only; browsers just close the tab)
 */
int main()
{
    snf::Application app(0, nullptr);

    // ── SNFCore timer ─────────────────────────────────────────────────────────
    // This timer fires through EventLoop::runPendingWork() inside each frame.
    // Its output in the browser console proves the SNF event loop is alive.
    snf::Timer ticker;
    int ticks = 0;
    ticker.timeout.connect([&]() {
        std::printf("SNF tick %d\n", ++ticks);
    });
    ticker.start(1000ms);

    // ── Web application node ──────────────────────────────────────────────────
    snf::widgets::WebApplicationNode webApp;

    bool showDemo = true;
    bool showMetrics = false;

    // ── initialized: set up ImGui context and backends ───────────────────────
    webApp.initialized.connect([&]() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;  // Disable imgui.ini (no filesystem in web).

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL(webApp.window(), true);

#ifdef __EMSCRIPTEN__
        // Installs wheel/pointer-lock callbacks so that scroll and mouse
        // behaviour work correctly inside the browser canvas.
        ImGui_ImplGlfw_InstallEmscriptenCallbacks(webApp.window(), "#canvas");
#endif

        ImGui_ImplOpenGL3_Init("#version 300 es");

        std::printf("ImGui %s initialised\n", ImGui::GetVersion());
    });

    // ── frame: ImGui new-frame → demo window → render → GL present ───────────
    webApp.frame.connect([&]() {
        // Start a new ImGui frame (reads GLFW input captured by glfwPollEvents
        // at the top of WebApplicationNode::tick()).
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ── ImGui UI ─────────────────────────────────────────────────────────
        if (showDemo) {
            ImGui::ShowDemoWindow(&showDemo);
        }

        // Small overlay showing that SNFCore timers are firing.
        {
            const ImGuiWindowFlags overlayFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoMove;

            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.55f);
            if (ImGui::Begin("SNFCore", nullptr, overlayFlags)) {
                ImGui::Text("SNFCore event loop active");
                ImGui::Text("Timer ticks: %d", ticks);
                ImGui::Separator();
                ImGui::Checkbox("ImGui demo", &showDemo);
                ImGui::Checkbox("Metrics", &showMetrics);
            }
            ImGui::End();
        }

        if (showMetrics) {
            ImGui::ShowMetricsWindow(&showMetrics);
        }

        // ── Render ────────────────────────────────────────────────────────────
        ImGui::Render();

        int fbW = 0;
        int fbH = 0;
        glfwGetFramebufferSize(webApp.window(), &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    });

    // ── shutdown: tear down ImGui backends ───────────────────────────────────
    // This is reached in native builds when the window is closed.
    // In browser builds the tab is simply closed; no explicit teardown needed.
    webApp.shutdown.connect([&]() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        std::printf("ImGui shutdown\n");
    });

    // Enters the Emscripten main loop (does not return on web).
    webApp.run();

    return 0;
}
