# ImGui + Emscripten + GLFW Web Demo

Objetivo: compilar una aplicación C++ con Dear ImGui para navegador, usando Emscripten, GLFW vía port de Emscripten, WebGL 2 y un `shell.html` propio.

El resultado será una web app que muestra la demo integrada de ImGui.

---

## 1. Requisitos previos

Se asume que `emsdk` ya está instalado y operativo.

Antes de compilar, carga el entorno de Emscripten:

```bash
source /ruta/a/emsdk/emsdk_env.sh
```

Comprueba que las herramientas están disponibles:

```bash
emcc --version
em++ --version
emcmake --version
```

---

## 2. Estructura del proyecto

Estructura final recomendada:

```txt
imgui-web-demo/
├── CMakeLists.txt
├── main.cpp
├── shell.html
└── third_party/
    └── imgui/
```

---

## 3. Descargar Dear ImGui

Desde la raíz del proyecto:

```bash
mkdir -p third_party
git clone https://github.com/ocornut/imgui.git third_party/imgui
```

Esto deja ImGui en:

```txt
third_party/imgui
```

No hace falta compilar GLFW upstream a mano. Para web se usará el port de GLFW proporcionado por Emscripten:

```cmake
--use-port=contrib.glfw3
```

---

## 4. Crear `shell.html`

Crear un fichero `shell.html` en la raíz del proyecto:

```html
<!doctype html>
<html lang="es">
<head>
    <meta charset="utf-8">
    <title>ImGui Web Demo</title>

    <style>
        html, body {
            margin: 0;
            padding: 0;
            width: 100vw;
            height: 100vh;
            overflow: hidden;
            background: #000;
        }

        #canvas {
            position: fixed !important;
            left: 0 !important;
            top: 0 !important;
            width: 100vw !important;
            height: 100vh !important;
            display: block !important;
            border: 0 !important;
            outline: none !important;
            background: #111;
            touch-action: none;
        }
    </style>
</head>

<body>
    <canvas id="canvas" oncontextmenu="event.preventDefault()" tabindex="-1"></canvas>

    <script>
        var Module = {
            canvas: document.getElementById("canvas")
        };
    </script>

    {{{ SCRIPT }}}
</body>
</html>
```

Notas importantes:

- `{{{ SCRIPT }}}` es obligatorio. Emscripten lo sustituye por el script que carga la aplicación.
- El canvas tiene que llamarse `canvas`, porque luego se referencia desde C++ con `#canvas`.
- Este `shell.html` sustituye al HTML por defecto de Emscripten y elimina el logo `powered by emscripten`.

---

## 5. Crear `main.cpp`

Crear `main.cpp`:

```cpp
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <GLES3/gl3.h>
#include <GLFW/glfw3.h>

struct AppState
{
    GLFWwindow* window = nullptr;
    bool show_demo_window = true;
    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
};

static void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

#ifdef __EMSCRIPTEN__
static void resize_to_browser()
{
    double css_width = 0.0;
    double css_height = 0.0;

    emscripten_get_element_css_size("#canvas", &css_width, &css_height);

    const int width = static_cast<int>(css_width);
    const int height = static_cast<int>(css_height);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    int current_width = 0;
    int current_height = 0;

    emscripten_get_canvas_element_size("#canvas", &current_width, &current_height);

    if (current_width != width || current_height != height)
    {
        emscripten_set_canvas_element_size("#canvas", width, height);
    }
}
#endif

static void main_loop(void* user_data)
{
    auto* app = static_cast<AppState*>(user_data);

#ifdef __EMSCRIPTEN__
    resize_to_browser();
#endif

    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (app->show_demo_window)
    {
        ImGui::ShowDemoWindow(&app->show_demo_window);
    }

    ImGui::Render();

    int display_w = 0;
    int display_h = 0;
    glfwGetFramebufferSize(app->window, &display_w, &display_h);

    glViewport(0, 0, display_w, display_h);

    const ImVec4& color = app->clear_color;
    glClearColor(color.x, color.y, color.z, color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(app->window);
}

int main()
{
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit())
    {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 300 es";

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(
        1280,
        720,
        "Dear ImGui Web Demo",
        nullptr,
        nullptr
    );

    if (window == nullptr)
    {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);

#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif

    ImGui_ImplOpenGL3_Init(glsl_version);

    static AppState app;
    app.window = window;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &app, 0, true);
#else
    while (!glfwWindowShouldClose(window))
    {
        main_loop(&app);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
#endif

    return 0;
}
```

Puntos importantes del `main.cpp`:

- `emscripten_set_main_loop_arg()` sustituye al bucle clásico `while` en navegador.
- `ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas")` es necesario para que funcionen correctamente ciertos eventos en web, especialmente el scroll del ratón.
- `resize_to_browser()` sincroniza el tamaño real del canvas con el tamaño visual del navegador.
- `#version 300 es` corresponde a WebGL 2 / OpenGL ES 3.

---

## 6. Crear `CMakeLists.txt`

Crear `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)

project(imgui_web_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

add_executable(imgui_web_demo
    main.cpp

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp

    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_web_demo PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_compile_options(imgui_web_demo PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Os
)

target_compile_definitions(imgui_web_demo PRIVATE
    IMGUI_DISABLE_FILE_FUNCTIONS
)

if (EMSCRIPTEN)
    target_link_options(imgui_web_demo PRIVATE
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sNO_FILESYSTEM=1
        -sDISABLE_EXCEPTION_CATCHING=1

        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2

        --use-port=contrib.glfw3
        --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html
    )

    set_target_properties(imgui_web_demo PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
    )
endif()
```

Notas importantes:

- No mezclar `--use-port=contrib.glfw3` con `-sUSE_GLFW=3`.
- Para esta ruta se usa solo:

```cmake
--use-port=contrib.glfw3
```

- `--shell-file` indica a Emscripten que use nuestro HTML en vez del HTML por defecto.

---

## 7. Compilar

Desde la raíz del proyecto:

```bash
source /ruta/a/emsdk/emsdk_env.sh
```

Compilar desde cero:

```bash
rm -rf build
mkdir build
cd build

emcmake cmake ..
cmake --build . -j
```

Resultado esperado:

```txt
build/web/index.html
build/web/index.js
build/web/index.wasm
```

---

## 8. Ejecutar con servidor HTTP local

No abrir `index.html` con doble clic. Hay que servirlo por HTTP.

Desde `build/web`:

```bash
python3 -m http.server 8000
```

Abrir en navegador:

```txt
http://localhost:8000
```

---

## 9. Problemas frecuentes

### 9.1. Aparece el logo `powered by emscripten`

Causa: se está usando el HTML por defecto de Emscripten.

Solución: comprobar que existe `shell.html` y que en `CMakeLists.txt` está:

```cmake
--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html
```

Después recompilar desde cero:

```bash
rm -rf build
mkdir build
cd build
emcmake cmake ..
cmake --build . -j
```

---

### 9.2. El scroll dentro de ventanas ImGui no funciona

Causa probable: faltan los callbacks específicos de Emscripten para GLFW.

Solución: después de:

```cpp
ImGui_ImplGlfw_InitForOpenGL(window, true);
```

añadir:

```cpp
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
```

El canvas debe existir en `shell.html`:

```html
<canvas id="canvas" oncontextmenu="event.preventDefault()" tabindex="-1"></canvas>
```

---

### 9.3. Error: `ImGui_ImplGlfw_InstallEmscriptenCallbacks` no existe

Causa: versión antigua de ImGui.

Comprobar:

```bash
grep -R "InstallEmscriptenCallbacks" third_party/imgui/backends/imgui_impl_glfw.*
```

Si no aparece, actualizar ImGui:

```bash
cd third_party/imgui
git pull
cd ../..
```

Luego recompilar desde cero.

---

### 9.4. ImGui solo ocupa una zona pequeña de la web

Causa: el tamaño CSS del canvas y el tamaño real interno del canvas no coinciden.

Solución: usar `resize_to_browser()`:

```cpp
#ifdef __EMSCRIPTEN__
static void resize_to_browser()
{
    double css_width = 0.0;
    double css_height = 0.0;

    emscripten_get_element_css_size("#canvas", &css_width, &css_height);

    const int width = static_cast<int>(css_width);
    const int height = static_cast<int>(css_height);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    int current_width = 0;
    int current_height = 0;

    emscripten_get_canvas_element_size("#canvas", &current_width, &current_height);

    if (current_width != width || current_height != height)
    {
        emscripten_set_canvas_element_size("#canvas", width, height);
    }
}
#endif
```

Y llamarla al principio de `main_loop()`:

```cpp
#ifdef __EMSCRIPTEN__
    resize_to_browser();
#endif
```

---

### 9.5. Error con `GLFW/glfw3.h`

Causa probable: se ha usado `cmake ..` en vez de `emcmake cmake ..`.

Solución:

```bash
rm -rf build
mkdir build
cd build

emcmake cmake ..
cmake --build . -j
```

---

### 9.6. Pantalla negra

Comprobar que WebGL 2 y GLSL están alineados.

En C++:

```cpp
const char* glsl_version = "#version 300 es";
```

En CMake:

```cmake
-sMIN_WEBGL_VERSION=2
-sMAX_WEBGL_VERSION=2
```

---

### 9.7. El navegador no carga `.wasm`

No abrir con `file://`.

Ejecutar:

```bash
cd build/web
python3 -m http.server 8000
```

Y abrir:

```txt
http://localhost:8000
```

---

## 10. Comandos completos desde cero

```bash
mkdir imgui-web-demo
cd imgui-web-demo

mkdir -p third_party
git clone https://github.com/ocornut/imgui.git third_party/imgui

# Crear main.cpp
# Crear CMakeLists.txt
# Crear shell.html

# Esto ya esta activado por defecto en mi bash:
# source /ruta/a/emsdk/emsdk_env.sh

rm -rf build
mkdir build
cd build

cmake --preset web-debug
cmake --build build/web-debug

cd build/web-debug/web
python3 -m http.server 8000
```

Abrir:

```txt
http://localhost:8000
```

---

## 11. Decisiones tomadas

- No se compila GLFW upstream a mano.
- Se usa GLFW mediante Emscripten:

```cmake
--use-port=contrib.glfw3
```

- Se usa WebGL 2:

```cmake
-sMIN_WEBGL_VERSION=2
-sMAX_WEBGL_VERSION=2
```

- Se usa un `shell.html` propio para controlar el canvas y eliminar el HTML por defecto de Emscripten.
- Se instala el callback específico de ImGui/GLFW/Emscripten para corregir eventos como el scroll.
- Se sincroniza el tamaño interno del canvas con el tamaño visual del navegador usando `emscripten_set_canvas_element_size()`.
