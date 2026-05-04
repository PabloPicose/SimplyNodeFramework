#include <SNFCore/Application.h>
#include <SNFHttpServer/HttpServer.h>
#include <SNFNetwork/HostAddress.h>
#include <SNFWebSocket/WebSocket.h>
#include <SNFWebSocket/WebSocketServer.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace snf;

namespace {

constexpr uint16_t kHttpPort = 8080;
constexpr uint16_t kWsPort   = 8765;

/**
 * Generates a ~1 MB structured binary payload that the server sends to every
 * newly connected WebSocket client.
 *
 * Binary layout
 * -------------
 *  Offset  Size  Description
 *  ------  ----  -----------
 *       0     4  Magic "SNFD"
 *       4     4  uint32_t version = 1
 *       8     4  uint32_t record_count
 *      12  N*16  Records:
 *                  0   uint64_t  timestamp_ms   (100 ms steps)
 *                  8   float     value_a         (sawtooth 0..1)
 *                 12   float     value_b         (sawtooth with phase shift)
 */
std::vector<uint8_t> generatePayload()
{
    constexpr uint32_t kRecordCount = 65500;  // 65500 * 16 = 1 048 000 bytes ≈ 1 MB
    constexpr size_t   kTotalSize   = 12 + static_cast<size_t>(kRecordCount) * 16;

    std::vector<uint8_t> buf(kTotalSize, 0);
    uint8_t*             p = buf.data();

    // Header
    std::memcpy(p, "SNFD", 4);
    p += 4;
    const uint32_t version = 1;
    std::memcpy(p, &version, 4);
    p += 4;
    std::memcpy(p, &kRecordCount, 4);
    p += 4;

    // Records
    for (uint32_t i = 0; i < kRecordCount; ++i) {
        const uint64_t ts = static_cast<uint64_t>(i) * 100;
        std::memcpy(p, &ts, 8);
        p += 8;

        const float va = static_cast<float>(i % 1000) * 0.001f;
        std::memcpy(p, &va, 4);
        p += 4;

        const float vb = static_cast<float>((i * 7 + 333) % 1000) * 0.001f;
        std::memcpy(p, &vb, 4);
        p += 4;
    }

    return buf;
}

}  // namespace

int main(int argc, char** argv)
{
    Application app(argc, argv);

    // The HTTP server serves whatever directory is pointed to by
    // SNF_EMS_SAVE_WEB_ROOT (default: ./web).
    // Point it to the Emscripten build output directory, e.g.:
    //   SNF_EMS_SAVE_WEB_ROOT=/path/to/build/web/ems_save_client ./ems_save_server
    const std::string webRoot = [&]() -> std::string {
        if (const char* env = std::getenv("SNF_EMS_SAVE_WEB_ROOT")) {
            return env;
        }
        return "./web";
    }();

    // ── HTTP server ──────────────────────────────────────────────────────────
    HttpServer httpServer;

    httpServer.on_started.connect([](uint16_t port) {
        std::cout << "HTTP  listening on http://127.0.0.1:" << port << "\n";
    });
    httpServer.on_error.connect([](const std::string& e) {
        std::cerr << "HTTP error: " << e << "\n";
    });

    std::cout << "Serving web assets from: " << webRoot << "\n";
    httpServer.serve_static("/", webRoot);

    if (!httpServer.listen(HostAddress::AnyIPv4, kHttpPort)) {
        std::cerr << "Failed to start HTTP server on port " << kHttpPort << "\n";
        return 1;
    }

    // ── Pre-generate the binary payload ─────────────────────────────────────
    const auto payload = generatePayload();
    std::cout << "Payload ready: " << payload.size() << " bytes ("
              << payload.size() / 1024 << " KB)\n";

    // ── WebSocket server ─────────────────────────────────────────────────────
    WebSocketServer wsServer;

    wsServer.newConnection.connect([&wsServer, &payload]() {
        while (auto ws = wsServer.nextPendingConnection()) {
            ws->setParent(&wsServer);
            const std::string peer = ws->peerAddress().toString();
            std::cout << "WS    client connected: " << peer << "\n";

            // Send the full payload as a single binary message.
            ws->sendBinaryMessage(payload);
            std::cout << "WS    sent " << payload.size() << " bytes to " << peer << "\n";
        }
    });

    wsServer.errorOccurred.connect([](const std::string& e) {
        std::cerr << "WebSocket server error: " << e << "\n";
    });

    if (!wsServer.listen(HostAddress::AnyIPv4, kWsPort)) {
        std::cerr << "Failed to start WebSocket server on port " << kWsPort << "\n";
        return 1;
    }
    std::cout << "WS    listening on ws://127.0.0.1:" << kWsPort << "\n";

    return app.run();
}
