# SimplyNodeFramework

[![Ubuntu](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-single-platform.yml/badge.svg)](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-single-platform.yml)
[![Coverage](https://codecov.io/gh/PabloPicose/SimplyNodeFramework/graph/badge.svg)](https://codecov.io/gh/PabloPicose/SimplyNodeFramework)
[![Rocky Linux 8](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-rockylinux8.yml/badge.svg)](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-rockylinux8.yml)
[![Debian 12](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-debian12.yml/badge.svg)](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-debian12.yml)

A modular C++ library providing an event-driven, node-based runtime.
Requires **C++17**. Primary target is **Linux** (epoll); **WebAssembly** (Emscripten) is supported for `SNFCore`.

| Package | Contents |
|---|---|
| **`SNFCore`** | Event loop, node ownership tree, timers, signals, cross-thread dispatch |
| **`SNFNetwork`** | TCP/Unix sockets (non-blocking, epoll-based). Linux only. |

---

## Installation

### Option A — git clone + add_subdirectory

```bash
# From your project root:
git clone https://github.com/PabloPicose/SimplyNodeFramework.git deps/SimplyNodeFramework
```

```cmake
set(SNF_ENABLE_TESTS    OFF CACHE BOOL "" FORCE)
set(SNF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/SimplyNodeFramework)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE SNFCore::SNFCore)
```

### Option B — FetchContent

```cmake
include(FetchContent)

if(NOT TARGET SNFCore::SNFCore)
    set(SNF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SimplyNodeFramework
        GIT_REPOSITORY https://github.com/PabloPicose/SimplyNodeFramework.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(SimplyNodeFramework)
endif()

add_executable(app main.cpp)
target_link_libraries(app PRIVATE SNFCore::SNFCore)
```

### Option C — find_package after installation

```bash
cmake -S . -B build && cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(SNFCore CONFIG REQUIRED)
target_link_libraries(app PRIVATE SNFCore::SNFCore)
```

---

## Quick start examples

### Timer (SNFCore)

```cpp
#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <iostream>

using namespace snf;
using namespace std::chrono_literals;

int main(int argc, char** argv)
{
    Application app(argc, argv);
    int ticks = 0;
    Timer timer;

    timer.timeout.connect([&]() {
        std::cout << "Tick " << ++ticks << "\n";
        if (ticks >= 5) { timer.stop(); app.quit(); }
    });

    timer.start(200ms);
    return app.run();
}
```

### TCP echo server + client (SNFNetwork)

```cpp
#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/HostAddress.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>
#include <iostream>

using namespace snf;
using namespace std::chrono_literals;

int main(int argc, char** argv)
{
    Application app(argc, argv);

    TcpServer server;
    server.listen(HostAddress::LocalHost, 9000);
    server.newConnection.connect([&]() {
        TcpSocket* peer = server.nextPendingConnection();
        peer->readyRead.connect([peer]()   { peer->write(peer->readAll()); });
        peer->disconnected.connect([peer]() { peer->deleteLater(); });
    });

    TcpSocket client;
    client.connectToHost(HostAddress::LocalHost, 9000);
    client.connected.connect([&]()  { client.write("Hello, SNF!\n"); });
    client.readyRead.connect([&]() {
        auto b = client.readAll();
        std::cout << std::string(b.begin(), b.end());
        app.quit();
    });
    client.errorOccurred.connect([&](const std::string& e) {
        std::cerr << e << "\n"; app.quit();
    });

    Timer::singleShot(3000ms, [&]() { app.quit(); });
    return app.run();
}
```

### Cross-thread signal with `moveToThread()` (SNFCore)

```cpp
#include <SNFCore/Application.h>
#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>
#include <SNFCore/NodePtr.h>
#include <SNFCore/Timer.h>
#include <future>
#include <iostream>
#include <thread>

using namespace snf;

class Receiver : public Node {
public:
    explicit Receiver(Node* p = nullptr) : Node(p) {}
    void slotRecv(int v) {
        std::cout << "Received " << v << " on thread " << std::this_thread::get_id() << '\n';
        Application::instance()->quit();
    }
private:
    void update() override {}
};

int main(int argc, char** argv)
{
    Application app(argc, argv);

    std::promise<std::thread::id> ready;
    std::thread worker([&]() {
        auto* loop = app.getOrCreateCurrentThreadEventLoop();
        ready.set_value(std::this_thread::get_id());
        Timer keepAlive; keepAlive.setSingleShot(true); keepAlive.start(500);
        loop->run();
    });

    auto* receiver = new Receiver();
    receiver->moveToThread(ready.get_future().get());

    NodePtr<Receiver> ptr(receiver);
    Signal<int> signal;
    signal.connect(ptr, &Receiver::slotRecv, ConnectionType::Queued);

    std::cout << "Emitting from main thread " << std::this_thread::get_id() << '\n';
    signal.emit(42);

    app.run();
    worker.join();
    return 0;
}
```

> For a detailed explanation see [docs/guides/thread-affinity.md](docs/guides/thread-affinity.md).

---

## Core Concepts

### Node ownership

Every `Node` belongs to a parent–child ownership tree. The parent destructs its
children. Root nodes (no parent) are managed by the `Application`.

```cpp
auto* child = new MyNode(parent);   // parent owns child
node->deleteLater();                // safe deferred deletion
```

### `NodePtr<T>` — safe references

Wraps a raw pointer + generation counter. Becomes `false` when the target is
deleted, preventing use-after-free.

```cpp
NodePtr<MyNode> ptr(node);
if (ptr) { ptr->doWork(); }
```

### Signals and connections

| `ConnectionType` | Delivery |
|---|---|
| `Direct` *(default)* | Synchronous on the emitter's thread |
| `Queued` | Posted to the receiver's `EventLoop` |

```cpp
Signal<int> sig;
sig.connect([](int v) { /* direct */ });
sig.connect(NodePtr<Worker>(w), &Worker::onValue, ConnectionType::Queued);
```

### Application and EventLoop

```cpp
Application app(argc, argv);
return app.run();   // blocks until quit()

// Register a new thread and post work to it:
app.getOrCreateCurrentThreadEventLoop();
loop->post([]() { /* runs on this thread */ });
```

### Timer

```cpp
Timer t;
t.timeout.connect([]() { /* every 500 ms */ });
t.start(500ms);

Timer::singleShot(1000ms, []() { /* once */ });
```

---

## Build options

| CMake variable | Default | Description |
|---|---|---|
| `SNF_ENABLE_TESTS` | `ON` if top-level | Build unit tests (GoogleTest) |
| `SNF_ENABLE_EXAMPLES` | `ON` if top-level | Build in-tree examples |
| `SNF_WEB_ASSEMBLY` | `OFF` | Build `SNFCore` for WebAssembly (requires Emscripten toolchain) |

---

## Guides

Step-by-step documentation lives in [`docs/guides/`](docs/guides/):

| Guide | Description |
|---|---|
| [Building with Emscripten](docs/guides/building-with-emscripten.md) | Compile an SNFWidgets app to WebAssembly and run it in a browser or Node.js — no platform `#ifdef`s needed |
| [Thread Affinity](docs/guides/thread-affinity.md) | Cross-thread signal delivery with `moveToThread()` and `Queued` connections |

---

## API Reference

Generate locally with [Doxygen](https://www.doxygen.nl):

```bash
doxygen docs/Doxyfile
# open docs/html/index.html
```

---

## TODO

### General
- [x] Ini parser
- [x] JSON wrapper (nlohmann/json)
- [x] HTTP server + request/response parser
- [ ] TLV Packet helper (`[magic:4][flags:1][type:2][len:4][payload:N]`)
- [ ] Embed static assets into binary

### Core
- [ ] File system watcher (inotify)
- [ ] Process management (spawn, stdout/stderr capture)
- [ ] Plugin system (runtime shared-library loading)
- [ ] Per-node update flag (skip `update()` when no work is pending)
- [ ] Logging framework (log levels, formatting, file output)

### Profiling
- [ ] Classes and macros for measuring execution time, memory usage, and other metrics
- [ ] Server profiler that collects and exposes metrics via a socket or HTTP endpoint
- [ ] Macro/function to measure momory usage of a class or function scope

### Network
- [x] UDP socket
- [x] WebSocket
- [ ] HTTP client
- [ ] Serial port

### Widgets
- [ ] SelectionModel
- [ ] ItemDelegate
- [ ] Validator

### Database
- [x] SQLite wrapper + SqlTableModel
- [ ] MySQL wrapper

### Audio
- [ ] ALSA wrapper

## Contributing

Contributions are welcome.

If you want to fix a bug, improve the documentation, or propose a new feature, please read the contribution guide first:

[CONTRIBUTING.md](CONTRIBUTING.md)