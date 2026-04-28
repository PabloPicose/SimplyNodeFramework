#include <iostream>
#include <cstdlib>
#include <limits>

#include <SNFCore/Application.h>
#include <SNFHttpServer/HttpServer.h>
#include <SNFNetwork/HostAddress.h>

using namespace snf;

namespace {

uint16_t resolvePort() {
  constexpr uint16_t defaultPort = 8080;

  const char* envPort = std::getenv("SNF_HTTPAPP_PORT");
  if (!envPort || !*envPort) {
    return defaultPort;
  }

  try {
    const auto parsed = std::stoul(envPort);
    if (parsed <= std::numeric_limits<uint16_t>::max()) {
      return static_cast<uint16_t>(parsed);
    }
  } catch (...) {
  }

  std::cerr << "Server warning: invalid SNF_HTTPAPP_PORT='" << envPort
            << "', using " << defaultPort << "\n";
  return defaultPort;
}

}  // namespace

int main(int argc, char **argv) {
  Application app(argc, argv);
  HttpServer server;
  const std::string webRoot = "./web";

  server.on_started.connect([](uint16_t port) {
    std::cout << "Server started on port " << port << "\n";
    std::cout << "Launch on: http://127.0.0.1:" << port << std::endl;
  });

  server.on_error.connect([](const std::string& errorMsg) {
    std::cerr << "Server error: " << errorMsg << "\n";
  });

  std::cout << "Serving web assets from: " << webRoot << "\n";
  server.serve_static("/", webRoot);

  if (!server.listen(HostAddress::AnyIPv4, resolvePort())) {
    return 1;
  }

  return app.run();
}
