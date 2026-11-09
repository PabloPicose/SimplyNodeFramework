// Echo server used by the Autobahn fuzzingclient (tests WebSocketServer).
#include <SNFCore/Application.h>
#include <SNFWebSocket/WebSocket.h>
#include <SNFWebSocket/WebSocketServer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace snf;

int main(int argc, char** argv)
{
    Application app(argc, argv);
    // Usage: SNFWebSocketEchoServer [port] [--allow-origin <origin>]...
    std::uint16_t port = 9001;
    std::vector<std::string> origins;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--allow-origin") == 0 && i + 1 < argc) {
            origins.emplace_back(argv[++i]);
        } else {
            port = static_cast<std::uint16_t>(std::atoi(argv[i]));
        }
    }

    WebSocketServer server;
    server.setAllowedOrigins(origins);
    if (!server.listen(HostAddress::AnyIPv4, port)) {
        std::fprintf(stderr, "listen failed on %u\n", port);
        return 1;
    }
    server.errorOccurred.connect([](const std::string& e) { std::fprintf(stderr, "server: %s\n", e.c_str()); });
    server.newConnection.connect([&server]() {
        while (WebSocket* peer = server.nextPendingConnection()) {
            peer->textMessageReceived.connect([peer](const std::string& m) { peer->sendTextMessage(m); });
            peer->binaryMessageReceived.connect(
                [peer](const std::vector<std::uint8_t>& m) { peer->sendBinaryMessage(m); });
            peer->disconnected.connect([peer]() { peer->deleteLater(); });
            peer->errorOccurred.connect([peer](const std::string&) { peer->deleteLater(); });
        }
    });

    std::printf("echo server listening on %u\n", server.serverPort());
    std::fflush(stdout);
    return app.run();
}
