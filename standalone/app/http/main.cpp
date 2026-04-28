#include <iostream>
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <limits>

#include <SNFCore/Application.h>
#include <SNFHttpServer/HttpServer.h>

using namespace snf;

namespace {

std::filesystem::path resolveWebRoot(const char* argv0) {
  std::vector<std::filesystem::path> candidates;

  // Explicit override via environment variable.
  if (const char* envRoot = std::getenv("SNF_HTTPAPP_WEB_ROOT")) {
    if (*envRoot) {
      candidates.emplace_back(envRoot);
    }
  }

#ifdef SNF_HTTPAPP_DEFAULT_WEB_ROOT
  candidates.emplace_back(SNF_HTTPAPP_DEFAULT_WEB_ROOT);
#endif

  // Relative to current working directory.
  candidates.emplace_back("./web");
  candidates.emplace_back("../web");
  candidates.emplace_back("../../web");
  candidates.emplace_back("./standalone/build-web/web");
  candidates.emplace_back("./standalone/build/app/http/web");

  // Relative to executable location.
  if (argv0 && *argv0) {
    std::error_code ec;
    std::filesystem::path exePath = std::filesystem::weakly_canonical(argv0, ec);
    if (!ec) {
      const auto exeDir = exePath.parent_path();
      candidates.push_back(exeDir / "web");
      candidates.push_back(exeDir.parent_path() / "web");
      candidates.push_back(exeDir.parent_path().parent_path() / "web");
    }
  }

  for (const auto& candidate : candidates) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) {
      continue;
    }

    if (std::filesystem::exists(canonical / "index.html") &&
        std::filesystem::exists(canonical / "index.js") &&
        std::filesystem::exists(canonical / "index.wasm")) {
      return canonical;
    }
  }

  return {};
}

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
  const auto webRoot = resolveWebRoot(argc > 0 ? argv[0] : nullptr);

  server.on_started.connect([](uint16_t port) {
    std::cout << "Server started on port " << port << "\n";
    std::cout << "Launch on: http://localhost:" << port << std::endl;
  });

  server.on_error.connect([](const std::string& errorMsg) {
    std::cerr << "Server error: " << errorMsg << "\n";
  });

  if (webRoot.empty()) {
    std::cerr << "Server error: could not locate web assets directory containing "
                 "index.html/index.js/index.wasm\n";
    return 1;
  }

  std::cout << "Serving web assets from: " << webRoot << "\n";
  server.serve_static("/", webRoot.string());

  if (!server.listen("127.0.0.1", resolvePort())) {
    return 1;
  }

  return app.run();
}
