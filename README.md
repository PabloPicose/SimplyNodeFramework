# SimplyNodeFramework

- [![Ubuntu (build + test)](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-single-platform.yml/badge.svg)](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-single-platform.yml)
- [![Coverage (Codecov)](https://codecov.io/gh/PabloPicose/SimplyNodeFramework/graph/badge.svg)](https://codecov.io/gh/PabloPicose/SimplyNodeFramework)
- [![Rocky Linux 8](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-rockylinux8.yml/badge.svg)](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-rockylinux8.yml)
- [![Debian 12](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-debian12.yml/badge.svg)](https://github.com/PabloPicose/SimplyNodeFramework/actions/workflows/cmake-debian12.yml)

A modular C++ library providing an event-driven, node-based runtime for Linux.
It consists of two independent CMake packages:

- **`SNFCore`** — event loop, node ownership model, timers, and signals.
- **`SNFNetwork`** — TCP and Unix domain socket networking, built on top of `SNFCore`.

Both packages require **C++17** and target **Linux** (epoll-based I/O).

## Current Scope (What Is Included)

This section defines the current scope of SimplyNodeFramework so project status
and expectations are explicit.

### SNFCore

- Application singleton and per-thread EventLoop lifecycle
- Node ownership tree (parent/child), root-node management, and deferred deletion
- NodePtr generation-based safe references (use-after-free protection)
- Signal/Connection system with Direct and Queued delivery
- Timer scheduling (single-shot and repeating) integrated with EventLoop
- Cross-thread marshaling patterns through EventLoop task posting

### SNFNetwork

- TcpSocket: non-blocking client socket, async read/write, and lifecycle signals
- TcpServer: listen/accept flow, pending connection queue, and server-side socket handoff
- LocalSocket: Unix domain socket client behavior with async integration
- LocalServer: Unix domain socket server with pending connection queue
- IOEvent epoll integration used by TCP and Local socket/server classes

### Current Platform and Design Boundaries

- Platform target is Linux only (epoll-based implementation)
- Public API is C++17
- Event-driven, non-blocking design is the primary execution model
- Core and Network are distributed as independent CMake packages (SNFCore and SNFNetwork)

---

## Installation

### FetchContent

```cmake
include(FetchContent)

if(NOT TARGET SNFCore::SNFCore OR NOT TARGET SNFNetwork::SNFNetwork)
    # Disable tests when consumed as a dependency.
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
# For networking support use SNFNetwork (automatically pulls in SNFCore):
# target_link_libraries(app PRIVATE SNFNetwork::SNFNetwork)
```

`SNF_ENABLE_TESTS` defaults to `ON` only when the project is the top-level CMake project.

The `if(NOT TARGET ...)` guard is useful when your project can be configured in environments where `SNFCore::SNFCore` or `SNFNetwork::SNFNetwork` may already exist.

The exported namespaced targets are `SNFCore::SNFCore` and `SNFNetwork::SNFNetwork`. Those are the recommended target names to use both in `if(NOT TARGET ...)` guards and in `target_link_libraries(...)`.


### find\_package after installation

Build and install the library once:

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix /usr/local
```

Then consume it from your project:

```cmake
find_package(SNFCore CONFIG REQUIRED)
find_package(SNFNetwork CONFIG REQUIRED)   # also pulls in SNFCore

add_executable(app main.cpp)
target_link_libraries(app PRIVATE SNFNetwork::SNFNetwork)
```

Available targets: `SNFCore`, `SNFCore::SNFCore`, `SNFNetwork`, `SNFNetwork::SNFNetwork`.

---

## Example 1 — Periodic timer (SNFCore)

This mini-app fires a timer every 200 ms, prints a counter, and quits after five ticks.

**`main.cpp`**

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

    // Connect a slot: the lambda fires on the timer's owner thread.
    timer.timeout.connect([&]() {
        std::cout << "Tick " << ++ticks << "\n";
        if (ticks >= 5) {
            timer.stop();
            app.quit();   // Unblocks app.run()
        }
    });

    timer.start(200ms);   // Repeating, 200 ms interval
    return app.run();     // Blocks the event loop until quit()
}
```

**`CMakeLists.txt`**

```cmake
target_link_libraries(app PRIVATE SNFCore)
```

---

## Example 2 — TCP echo server and client (SNFNetwork)

This mini-app starts a TCP echo server on port 9000, connects a client, sends a
message, prints the echoed response, and quits. All I/O is handled asynchronously
via the event loop.

**`main.cpp`**

```cpp
#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/HostAddress.h>
#include <SNFNetwork/TcpServer.h>
#include <SNFNetwork/TcpSocket.h>
#include <iostream>
#include <string>

using namespace snf;
using namespace std::chrono_literals;

int main(int argc, char** argv)
{
    Application app(argc, argv);

    // ── Server ──────────────────────────────────────────────────────────────
    TcpServer server;
    server.listen(HostAddress::LocalHost, 9000);

    server.newConnection.connect([&]() {
        TcpSocket* peer = server.nextPendingConnection();

        // Echo every incoming message back to the client.
        peer->readyRead.connect([peer]() {
            peer->write(peer->readAll());
        });

        // Free the server-side socket once the client disconnects.
        peer->disconnected.connect([peer]() {
            peer->deleteLater();
        });
    });

    // ── Client ──────────────────────────────────────────────────────────────
    TcpSocket client;
    client.connectToHost(HostAddress::LocalHost, 9000);

    client.connected.connect([&]() {
        client.write("Hello, SNF!\n");
    });

    client.readyRead.connect([&]() {
        auto bytes = client.readAll();
        std::cout << std::string(bytes.begin(), bytes.end());
        app.quit();
    });

    client.errorOccurred.connect([&](const std::string& err) {
        std::cerr << "Connection error: " << err << "\n";
        app.quit();
    });

    // Safety shutdown: quit after 3 s if something goes wrong.
    Timer::singleShot(3000ms, [&]() { app.quit(); });

    return app.run();
}
```

**`CMakeLists.txt`**

```cmake
target_link_libraries(app PRIVATE SNFNetwork)
```

---

## Core Concepts

### Node ownership and lifetime

`Node` is the abstract base class for every object managed by the framework.
Every node is either a *root node* (owned by the `Application`) or a *child* of
another node. The parent owns its children and deletes them on destruction.

```cpp
// Parent owns child: child is deleted when parent is deleted.
auto* child = new MyNode(parent);

// Adopt an existing node.
parent->addChild(orphan);

// Schedule deferred deletion through the event loop.
// Safe to call from within signal handlers or update().
node->deleteLater();
```

> **Important:** do not create a `Node` subclass on the stack and then call
> `addChild` or `deleteLater` on it — stack-allocated nodes must be destructed
> normally by going out of scope.

### `NodePtr<T>` — safe node references

`NodePtr<T>` wraps a raw node pointer together with a *generation counter*.
It becomes `false` (expired) automatically as soon as the target node is deleted,
preventing use-after-free.

```cpp
NodePtr<MyNode> ptr(node);
if (ptr) {
    ptr->doWork();   // Safe: node is still alive.
}
```

Always pass a `NodePtr<Receiver>` when connecting a signal to a member function
so that the connection is automatically invalidated when the receiver is deleted:

```cpp
signal.connect(NodePtr<MyNode>(node), &MyNode::onFired);
```

### Signals and connections

`Signal<Args...>` is a type-safe, thread-aware publish/subscribe mechanism.

| Connection type | Delivery | Use when |
|---|---|---|
| `Direct` *(default)* | Synchronous, on the emitter's thread | Emitter and receiver share the same thread |
| `Queued` | Posted to the receiver's `EventLoop` | Cross-thread signal delivery |

```cpp
Signal<int> sig;

// Direct connection (default)
Connection conn = sig.connect([](int v) { /* runs on emitter's thread */ });

// Queued connection — safe for cross-thread use
sig.connect(NodePtr<Worker>(worker), &Worker::onValue, ConnectionType::Queued);

// Disconnect explicitly
conn.disconnect();
```

Connections to a `NodePtr<Receiver>` are invalidated automatically when the
receiver is deleted.

### Application and EventLoop

`Application` owns the main thread's `EventLoop`. Each thread that calls
`getOrCreateCurrentThreadEventLoop()` gets its own independent `EventLoop`.

```cpp
Application app(argc, argv);
// ... create nodes, connect signals ...
return app.run();   // Blocks until app.quit() or the last EventLoop stops.
```

To post work onto a specific thread:

```cpp
eventLoop->post([]() { /* runs on the EventLoop's thread */ });
```

### Timer

`Timer` schedules repeating or one-shot callbacks on its owner thread.

```cpp
Timer t;
t.setInterval(500ms);
t.setSingleShot(false);
t.timeout.connect([]() { /* called every 500 ms */ });
t.start();

// Free-standing one-shot timer (self-destructs after firing)
Timer::singleShot(1000ms, []() { /* called once after 1 s */ });
```

---

## Build options

| CMake variable | Default | Description |
|---|---|---|
| `SNF_ENABLE_TESTS` | `ON` if top-level, `OFF` otherwise | Build unit tests with GoogleTest |

---

## API Reference

Full API documentation can be generated locally with [Doxygen](https://www.doxygen.nl):

```bash
doxygen docs/Doxyfile
```

Open `docs/html/index.html` in a browser.

## TODO list

- [x] Ini parser
- [x] Json wrapper using nlohmann/json
- [ ] Serial Port class
- [ ] TLV Packet (Type-Length-Value), helper class to send packets through TCP/Unix socket
- [x] WebSocket support
- [x] HTTP server
- [x] HTTP Request/Response parser
- [ ] Embed assets/data into the binary (e.g. for serving static files in an HTTP server)
- [ ] HTTP request handler (e.g. for REST APIs)
- [ ] ALSA audio support

### Widgets
- [ ] SelectionModel
- [ ] ItemDelegate: for rendering items in a view (e.g. buttons, checkboxes, etc.)
- [ ] Validator: for validating user input in widgets (e.g. regex, range, etc.)

### TLV
This is how I see the spec of the posible TLV packet:
`[magic:4][flags:1][type:2][payload_len:4][payload:N]`
Where: 
- `magic` = 4 Bytes "SNF1" means SimplyNodeFramework1
- `flags` = 1 Byte, bitfield for future use (e.g. compression, encryption, etc.)
- `type` = 2 Bytes, user-defined packet type identifier, first byte could be my system PING, the rest are user defined.
- `payload_len` = 4 Bytes, length of the payload in bytes (N)
- `payload` = N Bytes, the actual data of the packet