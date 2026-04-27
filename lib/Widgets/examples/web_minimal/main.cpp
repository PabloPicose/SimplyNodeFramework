#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFWidgets/WebApplicationNode.h>

#include <GLES3/gl3.h>

#include <cstdio>
#include <chrono>

using namespace std::chrono_literals;

/**
 * Minimal SNFWidgets web example.
 *
 * Demonstrates the integration between:
 *   - snf::Application (node registry + EventLoop)
 *   - snf::Timer       (SNFCore timer firing through runPendingWork)
 *   - snf::widgets::WebApplicationNode (Emscripten+GLFW bootstrap)
 *
 * A timer fires every second and prints a counter.
 * The frame signal clears the screen with a dark colour each frame.
 * No ImGui or any UI library is required; this example only validates the
 * build and event-loop integration.
 */
int main()
{
    snf::Application app(0, nullptr);

    // ── Timer ─────────────────────────────────────────────────────────────
    snf::Timer ticker;
    int ticks = 0;
    ticker.timeout.connect([&]() {
        std::printf("Tick %d\n", ++ticks);
    });
    ticker.start(1000ms);

    // ── Web application node ───────────────────────────────────────────────
    snf::widgets::WebApplicationNode webApp;

    webApp.initialized.connect([]() {
        std::printf("SNFWidgets: WebApplicationNode initialised\n");
    });

    webApp.frame.connect([&]() {
        // Minimal render: just clear the screen.
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(webApp.window(), &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
    });

    // run() enters the Emscripten main loop (does not return on web).
    webApp.run();

    return 0;
}
