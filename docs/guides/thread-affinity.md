# Thread Affinity and Cross-Thread Signals

This guide explains how thread affinity works in SimplyNodeFramework and how
to deliver signals across threads safely using `moveToThread()` and
`ConnectionType::Queued`.

---

## Concept

Every `Node` is **bound to the thread that constructed it**. That thread is called
the node's *owner thread*. When a `Queued` connection is used, the signal
payload is posted to the owner thread's `EventLoop` and executed there —
regardless of which thread emitted the signal.

This lets you write concurrent code without explicit locking: objects live on one
thread, signals cross thread boundaries automatically.

```
Main thread                Worker thread
──────────────             ──────────────────────
signal.emit(42)  ──────►  [EventLoop queue]
                           └─► slotRecv(42)   ← runs here
```

---

## Requirements

Before calling `moveToThread(threadId)` on a node, the target thread **must**
have already registered its `EventLoop` by calling:

```cpp
app.getOrCreateCurrentThreadEventLoop();
```

If it has not, `moveToThread` cannot find the target loop and will return `false`.

---

## Full example

The example below:
1. Spawns a worker thread and registers its `EventLoop`.
2. Creates a `Receiver` node on the main thread and moves it to the worker.
3. Emits a `Signal<int>` from main — the slot executes on the worker thread.

### `main.cpp`

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

// -------------------------------------------------------------------
// Receiver lives on whichever thread it is moved to.
// -------------------------------------------------------------------
class Receiver : public Node
{
public:
    explicit Receiver(Node* parent = nullptr) : Node(parent) {}
    ~Receiver() override = default;

    void slotRecv(int value)
    {
        std::cout << "Received " << value
                  << " on thread " << std::this_thread::get_id() << '\n';
        Application::instance()->quit();
    }

private:
    void update() override {}
};

// -------------------------------------------------------------------
int main(int argc, char** argv)
{
    Application app(argc, argv);

    // ── Worker thread ────────────────────────────────────────────────
    // The promise lets main block until the worker has registered its
    // EventLoop — the only synchronisation needed before moveToThread.
    std::promise<std::thread::id> ready;

    std::thread worker([&]() {
        auto* loop = app.getOrCreateCurrentThreadEventLoop();
        ready.set_value(std::this_thread::get_id());   // unblock main

        // Keep the loop alive long enough to process the queued signal.
        // Without this, the loop exits immediately (no work registered yet).
        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(500);

        loop->run();   // blocks until stop() or the last timer fires
    });

    // ── Set up the receiver on main, then migrate it ─────────────────
    auto* receiver = new Receiver();

    // Blocks until the worker's EventLoop is ready.
    const auto workerThreadId = ready.get_future().get();
    receiver->moveToThread(workerThreadId);

    // ── Connect signal using NodePtr (lifetime tracking) ─────────────
    NodePtr<Receiver> ptr(receiver);
    Signal<int> signal;

    // Queued: payload is posted to the receiver's EventLoop (worker thread).
    signal.connect(ptr, &Receiver::slotRecv, ConnectionType::Queued);

    // ── Emit from main ────────────────────────────────────────────────
    std::cout << "Emitting from main thread " << std::this_thread::get_id() << '\n';
    signal.emit(42);

    app.run();
    worker.join();
    return 0;
}
```

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.26)
project(ThreadAffinityExample LANGUAGES CXX)

set(SNF_ENABLE_TESTS    OFF CACHE BOOL "" FORCE)
set(SNF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/SimplyNodeFramework)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE SNFCore::SNFCore)
target_compile_features(app PRIVATE cxx_std_17)
```

### Expected output

```
Emitting from main thread 140234567890432
Received 42 on thread 140234512345600
```

The two thread IDs will differ, confirming cross-thread delivery.

---

## Why `promise/future` instead of a mutex?

The only thing that needs synchronisation is ensuring the worker's `EventLoop`
exists before `moveToThread` is called. `std::promise<std::thread::id>` is
the minimal, readable way to express "wait for this one-time event":

```cpp
// Worker side — sets the value once:
ready.set_value(std::this_thread::get_id());

// Main side — blocks until the value is available:
const auto id = ready.get_future().get();
```

No mutex, no condition variable, no shared boolean.

---

## Why `NodePtr<Receiver>` instead of a raw pointer?

`NodePtr<T>` tracks the node's *generation counter*. If `receiver` is deleted
before the queued task runs (e.g. due to a timeout or another path calling
`deleteLater()`), the slot is silently discarded instead of crashing.

```cpp
// Safe — slot is skipped if receiver was deleted before this runs.
signal.connect(ptr, &Receiver::slotRecv, ConnectionType::Queued);
```

Passing a raw pointer to `connect` is only possible with the free-function
overload, which has no lifetime tracking.

---

## Connection types at a glance

| `ConnectionType` | Delivery | Use when |
|---|---|---|
| `Direct` *(default)* | Synchronous on the emitter's thread | Same-thread connections |
| `Queued` | Posted to the receiver's `EventLoop` | Cross-thread signal delivery |

---

## Common mistakes

**Forgetting `ConnectionType::Queued`**
Without it, `Direct` is used: the slot runs on the emitter's thread, not the
receiver's. Thread affinity is not applied.

**Calling `moveToThread` before the worker registers its EventLoop**
`moveToThread` looks up the target EventLoop by thread ID at call time. If the
worker hasn't called `getOrCreateCurrentThreadEventLoop()` yet, the call fails
silently (returns `false`).

**Moving a node that has a parent on a different thread**
`moveToThread` only allows moving root nodes or complete subtrees. Moving a child
whose parent lives on a different thread is rejected.
