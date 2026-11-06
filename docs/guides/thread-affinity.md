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
| `Auto` *(default)* | `Direct` when emitted on the receiver's owner thread, `Queued` otherwise | Any receiver node |
| `Direct` | Synchronous on the emitter's thread | Thread-safe receivers that must run inside `emit()` |
| `Queued` | Posted to the receiver's `EventLoop`, even from the owner thread | Always deferring the slot to the next loop iteration |

`Auto` decides on every `emit()`, comparing the emitting thread with the
receiver's current owner, so it keeps working after `moveToThread()`. Keep in
mind that the same `emit()` may run the slot before returning (same thread) or
later on the receiver's loop (other thread): do not rely on the slot having run
when `emit()` returns. Arguments must be copyable, as with `Queued`.

A queued delivery always runs on the receiver's owner thread, even if the
receiver migrates between the `emit()` and the delivery: the task is forwarded
to the new owner.

---

## Hosting nodes vs running CPU work

The `Application` owns two pools with different jobs:

| Pool | Workers | Use it for |
|---|---|---|
| `ThreadPool` (`app.threadPool()`) | Each runs an `EventLoop` | Hosting nodes: `moveToThreadPool()` places a node on one worker for good |
| `TaskPool` (`app.taskPool()`) | Share one task queue, no `EventLoop` | CPU work: `Runnable`s and plain callables; a free worker takes the next task |

```cpp
// Heavy work off the main thread; the result comes back as a signal.
auto job = std::make_shared<ParseJob>(data);
job->finished.connect(NodePtr<Ui>(ui), &Ui::onParsed);  // Auto: runs on ui's thread
snf::TaskPool::globalInstance()->start(job);
```

A `TaskPool` worker cannot host ordinary nodes: constructing a root `Node`
there throws `std::logic_error`. A task must not wait for another task of the
same pool, or the pool can deadlock once every worker waits. `waitForDone()`
and `shutdown()` throw if called from one of the pool's own workers.

### Nodes that run on the TaskPool: `PooledNode`

A class derived from `snf::PooledNode` can live in a `TaskPool` instead of on
one thread. After `moveToTaskPool()` its slots run on whichever worker is free,
but never two at a time and always in the order they were queued, so the class
is still written without locks. Many pooled nodes spread over all the workers
by themselves, and a node that receives more work simply takes more turns.

```cpp
class Analyzer : public snf::PooledNode {
public:
    void onSample(double value);       // no locks needed
};

auto* analyzer = new Analyzer();
analyzer->moveToTaskPool();            // the Application's TaskPool
// sampleReady: a snf::Signal<double> of any node, on any thread
source->sampleReady.connect(NodePtr<Analyzer>(analyzer), &Analyzer::onSample);
```

Deriving from `PooledNode` means accepting its contract:

- The thread changes from one call to the next: do not keep per-thread state
  (`thread_local`, `ThreadStorage`, a per-thread database connection) across
  calls.
- `update()` is final and never called: there is no loop tick.
- No `Timer`, `Process` or I/O node in the subtree (anything whose
  `requiresEventLoop()` is true). `moveToTaskPool()` returns `false` for such a
  subtree, and creating one as a child inside a slot throws. To act on a
  schedule, connect a `Timer` that lives on a thread with an `EventLoop` to
  the pooled node.
- Children can be created inside a slot (they join the node's strand); root
  nodes cannot.
- Only whole subtrees move: a node with a parent is rejected.

Inside the node, `Auto` signals to nodes of the same subtree are synchronous;
signals to other nodes are queued to the receiver. `deleteLater()` destroys the
node inside its strand. `moveToThread()` takes it back to a fixed thread.
During `TaskPool::shutdown()` the pooled nodes keep processing their queued
work; the ones still alive afterwards are destroyed.

The migration hooks (`onAboutToMoveToThread`, `onMovedToThread`) receive
`nullptr` for the pool side of a move.

---

## Destroying nodes

A node may only be destroyed on its owner thread. While the owner `EventLoop`
is running it ticks the node (`run()`/`update()`) and dispatches its timers and
I/O, so a `delete` issued from another thread races with that work.

- On the owner thread, `delete node` is fine.
- From any other thread, call `node->deleteLater()`. The delete is handed to
  the owner loop (and follows the node if it migrates before the delete runs).
- If you need to wait for the destruction, post the `delete` to
  `node->ownerEventLoop()` and wait on a `std::promise`.

Destroying a node from another thread is only safe once its owner loop has
stopped (for example after `ThreadPool::shutdown()`), which is what
`Application` does when it is destroyed.

In Debug builds, `~Node()` prints a warning to `stderr` when a node is
destroyed from a thread other than its owner while the owner loop is running.

---

## Common mistakes

**Using `ConnectionType::Direct` with a receiver on another thread**
`Direct` runs the slot on the emitter's thread, not the receiver's, so thread
affinity is not applied. Leave the default (`Auto`) unless the receiver is
thread-safe. Connections to a lambda without a receiver node are always
`Direct`: pass a `NodePtr` receiver to get thread affinity.

**Calling `moveToThread` before the worker registers its EventLoop**
`moveToThread` looks up the target EventLoop by thread ID at call time. If the
worker hasn't called `getOrCreateCurrentThreadEventLoop()` yet, the call fails
silently (returns `false`).

**Moving a node to a worker of a pool that has shut down**
After `ThreadPool::shutdown()` its worker loops are retired: they never run
again. `moveToThread` to one of those workers returns `false`, and an
asynchronous request whose target is retired before it applies is dropped, so
the node stays where it was.

**Expecting `moveToThreadPool` to balance load over time**
It picks one worker when called (by default the one hosting fewer nodes) and
the node stays there. Call it from the node's owner thread when moving many
nodes in a row: called from another thread, the migration applies later and
consecutive calls may pick the same worker.

**Changing the tree from another thread**
`setParent()` and constructing a node with a parent mutate the parent's list of
children, so they must run on the owner thread of the nodes involved. In Debug
builds a warning is printed when this happens from another thread while the
owner loop is running (the same check as for destruction).

**Moving a node that has a parent on a different thread**
`moveToThread` only allows moving root nodes or complete subtrees. Moving a child
whose parent lives on a different thread is rejected.

**Deleting a migrated node from the thread that created it**
After `moveToThread`/`moveToThreadPool` the owner is the target thread. A
stack-allocated node going out of scope, or a plain `delete`, on the original
thread races with the worker loop. Use `deleteLater()` or delete it on the
owner thread (see *Destroying nodes*).
