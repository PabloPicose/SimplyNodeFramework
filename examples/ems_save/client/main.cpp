#include <SNFCore/Application.h>
#include <SNFWebSocket/WebSocket.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/FileSaveButton.h>
#include <SNFWidgets/FileOpenButton.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Window.h>

#include <cstdio>
#include <string>

/**
 * ems_save_client
 * ---------------
 * Connects to the ems_save_server WebSocket endpoint on port 8765, receives a
 * ~1 MB binary payload, and exposes two buttons:
 *
 *  "Save Received File"  — downloads the received file to the user's disk.
 *  "Open Local File…"    — lets the user pick any local file; its size is
 *                          shown as feedback (demonstrating FileOpenButton).
 *
 * Build (Emscripten):
 *   cmake --preset web-debug
 *   cmake --build build/web-debug --target ems_save_client
 *
 * Then run the native ems_save_server pointing at the output directory:
 *   SNF_EMS_SAVE_WEB_ROOT=build/web-debug/web/ems_save_client ./ems_save_server
 * and open http://127.0.0.1:8080 in your browser.
 */
int main()
{
    snf::Application app(0, nullptr);

    // ── UI ───────────────────────────────────────────────────────────────────
    snf::widgets::ApplicationNode webApp;
    webApp.setTitle("SNF File Save Demo");

    snf::widgets::Window win("File Transfer", &webApp);

    snf::widgets::Label statusLabel("Connecting to server…", &win);
    snf::widgets::Label sizeLabel("", &win);
    snf::widgets::FileSaveButton saveBtn("Save Received File", &win);
    saveBtn.setFilename("snf_data.bin");
    saveBtn.setMimeType("application/octet-stream");

    snf::widgets::Label         openResultLabel("", &win);
    snf::widgets::FileOpenButton openBtn("Open Local File\xe2\x80\xa6", &win);  // "…" (U+2026)

    // ── WebSocket client ─────────────────────────────────────────────────────
    snf::WebSocket ws;

    ws.connected.connect([&statusLabel]() {
        statusLabel.setText("Connected. Waiting for file…");
    });

    ws.disconnected.connect([&statusLabel]() {
        statusLabel.setText("Disconnected.");
    });

    ws.errorOccurred.connect([&statusLabel](const std::string& err) {
        statusLabel.setText("Error: " + err);
    });

    ws.binaryMessageReceived.connect(
        [&statusLabel, &sizeLabel, &saveBtn](const std::vector<uint8_t>& data) {
            saveBtn.setData(data);
            statusLabel.setText("File received! Click the button below to save it.");
            sizeLabel.setText("Received: " + std::to_string(data.size()) + " bytes ("
                              + std::to_string(data.size() / 1024) + " KB)");
        });

    // ── FileOpenButton demo ───────────────────────────────────────────────────
    openBtn.fileLoaded.connect(
        [&openResultLabel](const std::vector<uint8_t>& data, const std::string& name) {
            openResultLabel.setText(
                "Opened '" + name + "': " + std::to_string(data.size()) + " bytes");
        });

    // ── Start ────────────────────────────────────────────────────────────────
    webApp.initialized.connect([&ws]() {
        // Connect to the WebSocket server on the same host the page was loaded
        // from, but on port 8765.
        ws.connectToCurrentHost(8765);
    });

    webApp.run();
    return 0;
}
