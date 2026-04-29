#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>

#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/Window.h>

#include <SNFWebSocket/WebSocket.h>

namespace wg = snf::widgets;


#include <cstdio>


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

    // ── Web application root ──────────────────────────────────────────────────
    wg::ApplicationNode webApp;
    webApp.setTitle("SNFWidgets Standalone");



    // ── Widget tree ───────────────────────────────────────────────────────────
    wg::Window window("Hello from SNFWidgets", &webApp);
    // window.setFullSize(true);

    wg::VBoxLayout main_layout(&window);

    wg::HBoxLayout ip_layout(&main_layout);
    wg::LineEdit le_host("Host", &window);
    le_host.setPlaceholder("Host");
    ip_layout.addChild(&le_host);

    wg::LineEdit le_port("Port", &window);
    ip_layout.addChild(&le_port);

    main_layout.addChild(&ip_layout);


    // run() enters the Emscripten main loop (does not return on web).
    webApp.run();

    return 0;
}
