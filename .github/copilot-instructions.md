# Copilot Instructions for SimplyNodeFramework

## Build, Test, and Lint

**Configure and build (using presets):**
```bash
cmake --preset native-debug
cmake --build --preset native-debug
```

Available presets: `native-debug`, `native-release`, `native-debug-experimental`, `web-debug`, `web-release`.

**Run all tests:**
```bash
ctest --preset native-debug
```

**Run a single test by name:**
```bash
ctest --preset native-debug -R "SNFCoreTests.<TestName>"
```

**Format code:**
```bash
clang-format -i <file>
```

**Generate API docs:**
```bash
doxygen docs/Doxyfile
```

**WebAssembly build** requires the Emscripten toolchain active via `emcmake`:
```bash
emcmake cmake --preset web-debug
cmake --build --preset web-debug
```

## Architecture

SimplyNodeFramework is a modular C++ 17 library providing an event-driven, node-based runtime inspired by Qt but without the MOC tooling. Primary target is Linux (epoll); WebAssembly is supported for `SNFCore`.

### Modules

| Target | Description | Depends on |
|--------|-------------|------------|
| `SNFCore` | Event loop, node tree, timers, signals, thread dispatch | pthread |
| `SNFNetwork` | TCP/Unix sockets, UDP (epoll-based, Linux only) | SNFCore |
| `SNFJson` | nlohmann/json wrapper | SNFCore |
| `SNFDatabase` | SQLite wrapper + `SqlTableModel` (opt-in: `SNF_ENABLE_SQLITE=ON`) | SNFCore |
| `SNFHttpServer` | HTTP server + request/response parser (Linux only) | SNFCore |
| `SNFWebSocket` | WebSocket client/server (Linux and WebAssembly) | SNFCore |
| `SNFSnmp` | SNMP client/agent (opt-in: `SNF_ENABLE_SNMP=ON`, Linux only) | SNFCore, net-snmp |
| `SNFWidgets` | Dear ImGui + GLFW integration (optional, desktop + WebAssembly) | SNFCore |
| `SNFProfiler` | Profiling: trace ring buffers, memory tracking, WebSocket dashboard (opt-in: `SNF_ENABLE_PROFILING=ON`, Linux only) | SNFCore, SNFWebSocket |

Each module lives under `lib/<ModuleName>/` with its own `CMakeLists.txt`, `src/`, and (for SNFCore) a `test/` directory.

### Core Concepts

**Node ownership tree** — every `Node` is owned by its parent and destroyed when the parent is destroyed. Root nodes are tracked by `Application`. Nodes must be heap-allocated (`new`); stack allocation is only safe for leaf nodes that never call `deleteLater()` or get parented.

**`NodePtr<T>`** — safe reference wrapper with a generation counter. Goes `false` when the target is deleted. Always prefer `NodePtr<T>` over raw pointers when storing references to nodes.

**Signal/slot system** — type-safe via `Signal<Args...>`. Two delivery modes:
- `ConnectionType::Direct` (default): synchronous, on the emitter's thread.
- `ConnectionType::Queued`: posted to the receiver's `EventLoop`; required for cross-thread connections.

**Thread affinity** — each node is bound to the thread that created it. Call `node->moveToThread(threadId)` to rebind. Use `Queued` connections for cross-thread signal delivery. See `docs/guides/thread-affinity.md` for details.

**Application / EventLoop** — `Application::run()` blocks until `quit()`. Additional threads get their own loop via `app.getOrCreateCurrentThreadEventLoop()`.

## Key Conventions

- All framework types are in `namespace snf`.
- Headers use `#pragma once`.
- Subclasses of `Node` must override `update()`, which is called once per event-loop iteration.
- Use `deleteLater()` for deferred, safe node deletion; never `delete` a node directly from a signal handler.
- The `SNF_PLATFORM_WEB` compile definition is set only for SNFCore's own TUs in WebAssembly mode; downstream code should use the standard `__EMSCRIPTEN__` macro.
- Optional features are controlled by CMake options: `SNF_ENABLE_SQLITE`, `SNF_ENABLE_SNMP`, `SNF_EXPERIMENTAL`, `SNF_ENABLE_PROFILING`. These default `OFF`; the `native-debug` preset enables SQLite.
- `SNF_ENABLE_PROFILING=ON` builds `SNFProfiler` and auto-wires it into `Application` via `__attribute__((weak))` symbols. Executables that don't link `SNFProfiler` are unaffected (weak no-ops). Profiler macros: `TRACE_EVENT("cat")`, `TRACE_EVENT("cat","name")`, `TRACE_EVENT_BEGIN/END`, `TRACE_MEMORY_ALLOC/FREE` — all zero-cost no-ops when profiling is off.
- The React profiler dashboard lives in `lib/Profiler/ProfilerTool/` (React 19 + TypeScript + Vite). It has no charting library dependencies — all charts are raw SVG.
- `SNF_EXPERIMENTAL` gates `lib/experimental/` (e.g., `AsyncTask`, `AsyncTaskSequence`).
- clang-format style: Google-based, 4-space indent, 120-column limit, left pointer alignment (`int* p`). Run `clang-format` before committing.
- GoogleTest is fetched automatically via `FetchContent` when `SNF_ENABLE_TESTS=ON`.
- Branch naming: `feature/`, `fix/`, `docs/`, `refactor/` prefixes.
