#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>

#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/Window.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/RadioButton.h>
#include <SNFWidgets/SpinBox.h>
#include <SNFWidgets/TextEdit.h>

#include <cstdio>

using namespace std::chrono_literals;

/**
 * Minimal standalone SNFWidgets web application.
 *
 * This file is the entire application: no imgui.h, no GLFW includes,
 * no manual ImGui frame management.  Just widgets and signals.
 *
 * Build:
 *   source /path/to/emsdk/emsdk_env.sh
 *   emcmake cmake -S . -B build -G Ninja
 *   cmake --build build
 *   cd build/web && python3 -m http.server 8000
 */
int main()
{
    snf::Application app(0, nullptr);

    // ── SNFCore timer ─────────────────────────────────────────────────────────
    // Fires once per second; output is visible in the browser console (F12).
    snf::Timer ticker;
    int ticks = 0;
    ticker.timeout.connect([&]() {
        std::printf("tick %d\n", ++ticks);
    });
    ticker.start(1000ms);

    // ── Web application root ──────────────────────────────────────────────────
    snf::widgets::ApplicationNode webApp;
    webApp.setTitle("SNFWidgets Standalone");

    // ── Widget tree ───────────────────────────────────────────────────────────
    snf::widgets::Window     window("Hello from SNFWidgets", &webApp);
    snf::widgets::PushButton button("Click me!",             &window);

    snf::widgets::LineEdit lineEdit("Type something...", &window);

    button.clicked.connect([&ticks]() {
        std::printf("Button clicked — ticks so far: %d\n", ticks);
    });
    return app.run();
}
