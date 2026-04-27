#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFWidgets/WebApplicationNode.h>
#include <SNFWidgets/ImGuiDemoWindow.h>
#include <SNFWidgets/Window.h>
#include <SNFWidgets/PushButton.h>

#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

/**
 * SNFWidgets + ImGui demo.
 *
 * Shows how to build a web application using only the SNFWidgets API:
 *   - No imgui.h includes in application code.
 *   - No manual ImGui frame lifecycle (NewFrame / Render / etc.).
 *   - Widgets are SNF nodes: attach them as children, connect signals.
 *
 * The ImGui demo window is provided as a first-class widget
 * (snf::widgets::ImGuiDemoWindow).  A custom Window and PushButton
 * demonstrate the component model, and an SNFCore Timer shows that the
 * event loop runs correctly each frame.
 */
int main()
{
    snf::Application app(0, nullptr);

    // ── SNFCore timer ─────────────────────────────────────────────────────
    // Fires every second through EventLoop::runPendingWork(); output visible
    // in the browser console (F12 → Console).
    snf::Timer ticker;
    int ticks = 0;
    ticker.timeout.connect([&]() {
        std::printf("SNF tick %d\n", ++ticks);
    });
    ticker.start(1000ms);

    // ── Application node ─────────────────────────────────────────────────
    snf::widgets::WebApplicationNode webApp;
    webApp.setTitle("SNFWidgets Demo");

    // ── ImGui built-in demo window ────────────────────────────────────────
    // Exercises the full ImGui widget catalogue without any imgui.h include.
    snf::widgets::ImGuiDemoWindow demoWin(&webApp);

    // ── Custom window with widgets ────────────────────────────────────────
    snf::widgets::Window window("SNFCore integration", &webApp);

    snf::widgets::PushButton button("Click me!", &window);
    button.clicked.connect([&ticks]() {
        std::printf("Button clicked — SNFCore ticks so far: %d\n", ticks);
    });

    // run() enters the Emscripten main loop (does not return on web).
    webApp.run();

    return 0;
}
