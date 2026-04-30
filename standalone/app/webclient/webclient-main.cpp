#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/HostAddress.h>
#include <SNFWebSocket/WebSocket.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/CheckBox.h>
#include <SNFWidgets/ImGuiDemoWindow.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/ProgressBar.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/RadioButton.h>
#include <SNFWidgets/SpinBox.h>
#include <SNFWidgets/TableView.h>
#include <SNFWidgets/Window.h>

namespace wg = snf::widgets;

#include <cstdio>

class CustomTableModel final : public snf::AbstractTableModel
{
public:
    int rowCount() const override { return 3; }
    int columnCount() const override { return 3; }
    std::string data(int row, int column) const override
    {
        return "R" + std::to_string(row) + "C" + std::to_string(column);
    }

    std::string headerData(int section) const override { return "Col " + std::to_string(section); }
};

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

    snf::WebSocket ws;
    ws.errorOccurred.connect([](std::string err) {
        //
        std::printf("WebSocket error: %s\n", err.c_str());
    });

    // ── SNFCore timer ─────────────────────────────────────────────────────────
    // Fires once per second; output is visible in the browser console (F12).

    // ── Web application root ──────────────────────────────────────────────────
    wg::ApplicationNode webApp;
    webApp.setTitle("SNFWidgets Standalone");

    // ── Widget tree ───────────────────────────────────────────────────────────
    wg::Window window("Hello from SNFWidgets", &webApp);
    // window.setFullSize(true);

    wg::VBoxLayout main_layout(&window);
    window.setLayout(&main_layout);

    wg::HBoxLayout ip_layout(&main_layout);
    main_layout.addWidget(&ip_layout);

    wg::LineEdit le_host("Host", &window);
    le_host.setText("127.0.0.1");
    le_host.setPlaceholder("Host");
#ifdef __EMSCRIPTEN__
    le_host.setText(snf::WebSocket::currentOriginAddress().toString());
#endif
    ip_layout.addWidget(&le_host, 1);

    wg::SpinBox le_port("Port", 3000, 32000, &window);
    le_port.setValue(30123);
    ip_layout.addWidget(&le_port, 1);

    wg::PushButton pb_connect("Connect");
    pb_connect.clicked.connect([&]() {
#ifdef __EMSCRIPTEN__
        const std::uint16_t port = static_cast<std::uint16_t>(le_port.value());
        ws.connectToCurrentHost(port);
        std::printf("Connecting to WebSocket on current browser host:%u...\n", port);
#else
        snf::HostAddress host(le_host.text());
        std::uint16_t port = static_cast<std::uint16_t>(le_port.value());
        if (! host.isValid()) {
            std::printf("Invalid host: %s\n", host.toString().c_str());
            return;
        }
        ws.connectToHost(host, port);
        std::printf("Connecting to ws://%s:%u...\n", host.toString().c_str(), port);
#endif
    });

    ip_layout.addWidget(&pb_connect);

    wg::HBoxLayout conn_status_layout(&window);
    main_layout.addWidget(&conn_status_layout);

    wg::RadioButton rb_connected("Connected");
    conn_status_layout.addWidget(&rb_connected, 1);

    wg::RadioButton rb_disconnected("Disconnected");
    conn_status_layout.addWidget(&rb_disconnected, 1);

    rb_connected.link(&rb_disconnected);
    rb_disconnected.setSelected(true);

    wg::ImGuiDemoWindow demo(&webApp);

    ws.connected.connect([&]() {
        std::printf("WebSocket connected!\n");
        rb_connected.setSelected(true);
    });

    ws.disconnected.connect([&]() {
        std::printf("WebSocket disconnected.\n");
        rb_disconnected.setSelected(true);
    });
    // Test table
    CustomTableModel model;
    wg::TableView table(&window);
    table.setModel(&model);
    main_layout.addWidget(&table);
    // run() enters the Emscripten main loop (does not return on web).
    webApp.run();

    return 0;
}
