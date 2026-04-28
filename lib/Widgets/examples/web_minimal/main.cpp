#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFWidgets/ApplicationNode.h>

#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

/**
 * Minimal SNFWidgets web example.
 *
 * Demonstrates the integration between snf::Timer (SNFCore) and
 * snf::widgets::WebApplicationNode (Emscripten+GLFW+ImGui bootstrap).
 *
 * No imgui.h, no GLFW headers, no manual GL calls required.
 * WebApplicationNode manages the full frame lifecycle internally.
 */
int main()
{
    snf::Application app(0, nullptr);

    // ── SNFCore timer ─────────────────────────────────────────────────────────
    snf::Timer ticker;
    int ticks = 0;
    ticker.timeout.connect([&]() {
        std::printf("Tick %d\n", ++ticks);
    });
    ticker.start(1000ms);

    // ── Web application node ──────────────────────────────────────────────────
    snf::widgets::ApplicationNode webApp;
    webApp.setTitle("SNFWidgets Minimal");

    webApp.initialized.connect([]() {
        std::printf("SNFWidgets: ready\n");
    });

    // run() enters the Emscripten main loop (does not return on web).
    webApp.run();

    return 0;
}
