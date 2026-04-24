# SimplyNodeFramework

A modular C++ library providing an event-driven, node-based runtime for Linux.
It consists of two independent CMake packages:

- **`SNFCore`** — event loop, node ownership model, timers, and signals.
- **`SNFNetwork`** — TCP and Unix domain socket networking, built on top of `SNFCore`.

Both packages require **C++17** and target **Linux** (epoll-based I/O).

---

## Installation

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    SimplyNodeFramework
    GIT_REPOSITORY https://github.com/<owner>/SimplyNodeFramework.git
    GIT_TAG main
)

# Disable tests when consumed as a dependency
set(SNF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SimplyNodeFramework)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE SNFCore)
# For networking support use SNFNetwork (automatically pulls in SNFCore):
# target_link_libraries(app PRIVATE SNFNetwork)
```

`SNF_ENABLE_TESTS` defaults to `ON` only when the project is the top-level CMake project.

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
target_link_libraries(app PRIVATE SNFNetwork)
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