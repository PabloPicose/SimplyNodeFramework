#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>

#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/Window.h>

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
    snf::widgets::Window window("Hello from SNFWidgets", &webApp);

    snf::widgets::LineEdit lineEdit("Type something...", &window);

    snf::widgets::VBoxLayout mainLayout(&window);
    window.setLayout(&mainLayout);

    snf::widgets::HBoxLayout hbox;
    mainLayout.addWidget(&hbox);

    snf::widgets::PushButton button("Click me!", &window);
    snf::widgets::ProgressBar progressBar(0, 100, &window);
    snf::widgets::PushButton button2("Another click", &window);

    hbox.addWidget(&button);
    // hbox.addWidget(&lineEdit, 1);
    hbox.addWidget(&button2);
    mainLayout.addWidget(&progressBar);

    button.clicked.connect([&ticks, &progressBar]() {
        std::printf("Button clicked — ticks so far: %d\n", ticks);
        progressBar.setValue((progressBar.value() + 10) % 110);
    });

    // run() enters the Emscripten main loop (does not return on web).
    webApp.run();

    return 0;
}
