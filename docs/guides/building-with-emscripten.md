# Building an SNFWidgets Application with Emscripten (WebAssembly)

This guide walks you through creating an SNFWidgets application that compiles to
WebAssembly with Emscripten.

`SNFWidgets` wraps Dear ImGui and GLFW and abstracts all platform and event-loop
details. **No `#ifdef __EMSCRIPTEN__`**, no manual `emscripten_set_main_loop`,
no `runPendingWork()` calls are needed in application code. The same source
file works on desktop Linux and in the browser.**

> **Package requirements:**
> - `SNFWidgets` — widget tree, ImGui integration, GLFW, main loop abstraction.
> - `SNFCore` — pulled in transitively by `SNFWidgets`.
> - `SNFNetwork` / `SNFWebSocket` — optional; only add them if you need networking.

---

## Prerequisites

- CMake ≥ 3.26
- [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html)

### Install and activate emsdk

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh   # adds emcc / em++ to PATH
```

Verify:

```bash
emcc --version
```

---

## Project layout

```
my-app/
├── CMakeLists.txt
├── main.cpp
└── deps/
    └── SimplyNodeFramework/   ← cloned here
```

Clone the library:

```bash
mkdir -p my-app/deps
cd my-app/deps
git clone https://github.com/PabloPicose/SimplyNodeFramework.git
```

---

## The application

### `main.cpp`

`ApplicationNode::run()` drives the entire loop — on Emscripten it hands control
to `requestAnimationFrame`; on desktop it runs a standard GLFW while-loop.
Application code never deals with either platform directly.

```cpp
#include <SNFCore/Application.h>
#include <SNFCore/Timer.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/Window.h>

#include <cstdio>

namespace wg = snf::widgets;

int main()
{
    snf::Application app(0, nullptr);

    // ── Application root ──────────────────────────────────────────────────────
    // Owns the GLFW window, the Dear ImGui context, and the SNFCore event loop.
    // Works identically on desktop and in the browser — no #ifdefs required.
    wg::ApplicationNode appNode;
    appNode.setTitle("My SNFWidgets App");

    // ── Widget tree ───────────────────────────────────────────────────────────
    wg::Window win("Main Window", &appNode);
    win.setFullSize(true);
    win.setCollapsible(false);
    win.setResizable(false);

    wg::VBoxLayout layout(&win);
    win.setLayout(&layout);

    wg::PushButton btn("Click me");
    layout.addWidget(&btn);

    int count = 0;
    btn.clicked.connect([&]() {
        ++count;
        std::printf("Button clicked %d time(s)\n", count);
    });

    // SNFCore timers work normally — ApplicationNode calls
    // EventLoop::runPendingWork() every frame internally.
    snf::Timer ticker;
    ticker.timeout.connect([&]() {
        std::printf("Tick (count = %d)\n", count);
    });
    ticker.start(std::chrono::milliseconds(1000));

    // Does not return on Emscripten; blocks until window close on desktop.
    appNode.run();

    return 0;
}
```

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.26)
project(MyWebApp LANGUAGES CXX)

# ── Pull in SimplyNodeFramework ───────────────────────────────────────────────
set(SNF_ENABLE_TESTS    OFF CACHE BOOL "" FORCE)
set(SNF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SNF_WEB_ASSEMBLY    ON  CACHE BOOL "" FORCE)  # set OFF for a native build

add_subdirectory(deps/SimplyNodeFramework)

# ── Application target ────────────────────────────────────────────────────────
add_executable(app main.cpp)

# SNFWidgets transitively provides SNFCore, Dear ImGui, and the Emscripten
# GLFW3 port — no need to link anything else unless you use SNFNetwork.
target_link_libraries(app PRIVATE SNFWidgets::SNFWidgets)
target_compile_features(app PRIVATE cxx_std_17)

# Emscripten-specific options
if(EMSCRIPTEN)
    target_link_options(app PRIVATE
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1
    )
    # Emit a self-contained .html file instead of a bare .js module.
    set_target_properties(app PROPERTIES SUFFIX ".html")
endif()
```

---

## Configure and build

```bash
cd my-app

# Configure via emcmake so the Emscripten toolchain is injected automatically.
emcmake cmake -S . -B build-web

# Build
cmake --build build-web
```

Output: `build-web/app.html`, `app.js`, `app.wasm`.

---

## Run

### In a browser

Serve the output directory with any static file server and open the `.html` file:

```bash
cd build-web
python3 -m http.server 8080
# Open http://localhost:8080/app.html
```

### In Node.js (headless / CI)

```bash
node build-web/app.js
```

Timer and signal output appears in the terminal.

---

## Native build (same source, no Emscripten)

```bash
# No emcmake — plain cmake. SNF_WEB_ASSEMBLY defaults to OFF.
cmake -S . -B build-native
cmake --build build-native
./build-native/app
```

The exact same `main.cpp` runs on desktop without any changes.

---

## What `ApplicationNode` handles for you

| Concern | Without `ApplicationNode` | With `ApplicationNode` |
|---|---|---|
| Main loop | `emscripten_set_main_loop` / GLFW while | Handled in `run()` |
| SNFCore event loop | `EventLoop::runPendingWork()` each frame | Called inside `tick()` automatically |
| Dear ImGui lifecycle | `ImGui::NewFrame()`, `ImGui::Render()`, … | Fully internal |
| Canvas resize (web) | Manual `emscripten_get_element_css_size` | Handled in `tick()` |
| Platform `#ifdef`s | Many | None needed in application code |

---

## Signals on `ApplicationNode`

| Signal | When emitted | Typical use |
|---|---|---|
| `initialized` | Once, after GLFW + ImGui are ready | Load GL resources, textures |
| `frame` | Every frame, after ImGui has been rendered | Raw OpenGL drawing |
| `shutdown` | Before window is destroyed (desktop only) | Clean up GL resources |

Example:

```cpp
appNode.initialized.connect([&]() {
    std::printf("GL context ready — load textures here\n");
});

appNode.frame.connect([&]() {
    // Raw OpenGL calls after ImGui has rendered.
});
```

---

## Troubleshooting

**`EMSCRIPTEN toolchain detected but SNF_WEB_ASSEMBLY is OFF`**  
Always run `emcmake cmake …` (not plain `cmake`) when targeting WebAssembly.

**`SNF_WEB_ASSEMBLY=ON but the Emscripten toolchain is not active`**  
You set the flag but forgot `emcmake`. Use `emcmake cmake …`.

**Timers or signals don't fire**  
Make sure you are using `ApplicationNode::run()`. Do **not** call
`Application::run()` or `EventLoop::run()` manually — `ApplicationNode` handles
the event loop internally.

**Blank page in the browser**  
Open the browser DevTools console (F12). Most startup errors (missing `.wasm`,
missing canvas element) are reported there.
