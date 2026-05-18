cmake_minimum_required(VERSION 3.26)

project(SimplyNodeFramework VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Platform options ─────────────────────────────────────────────────────────
# SNF_WEB_ASSEMBLY: build SNFCore for WebAssembly/Emscripten.
# The Emscripten toolchain must be active (i.e. run CMake via emcmake or set
# CMAKE_TOOLCHAIN_FILE to the Emscripten.cmake module).
option(SNF_WEB_ASSEMBLY
    "Build SNFCore for WebAssembly/Emscripten (requires the Emscripten toolchain)"
    OFF)
option(SNF_ENABLE_TESTS "Enable tests for SimplyNodeFramework" ${PROJECT_IS_TOP_LEVEL})
option(SNF_ENABLE_EXAMPLES "Build SimplyNodeFramework examples" ${PROJECT_IS_TOP_LEVEL})
option(SNF_ENABLE_WEB_TESTS
    "Enable Node-runnable WebAssembly smoke tests when SNF_WEB_ASSEMBLY is ON"
    ${PROJECT_IS_TOP_LEVEL})
option(SNF_ENABLE_STANDALONE "Build the in-tree standalone examples" ${PROJECT_IS_TOP_LEVEL})
option(SNF_EXPERIMENTAL
    "Build experimental SNF modules such as AsyncTask and AsyncTaskSequence"
    OFF)
option(SNF_ENABLE_SQLITE
    "Enable SQLite support in SNFDatabase"
    OFF)
option(SNF_ENABLE_MYSQL
    "Enable MySQL support in SNFDatabase"
    OFF)
option(SNF_ENABLE_SNMP
    "Build the SNFSnmp module (requires net-snmp / libsnmp-dev)"
    OFF)
option(SNF_ENABLE_PROFILING
    "Build the SNFProfiler module (profiling with WebSocket dashboard)"
    OFF)
option(SNF_ENABLE_WIDGETS
    "Build SNFWidgets (Dear ImGui + GLFW integration, desktop only)"
    ON)

# Safety checks: mismatching toolchain / option causes a clear configure error.
if(EMSCRIPTEN AND NOT SNF_WEB_ASSEMBLY)
    message(FATAL_ERROR
        "Emscripten toolchain detected but SNF_WEB_ASSEMBLY is OFF.\n"
        "Pass -DSNF_WEB_ASSEMBLY=ON when configuring with emcmake, "
        "or use a native toolchain.")
endif()

if(SNF_WEB_ASSEMBLY AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "SNF_WEB_ASSEMBLY=ON but the Emscripten toolchain is not active.\n"
        "Run CMake through emcmake (or set CMAKE_TOOLCHAIN_FILE), "
        "or set SNF_WEB_ASSEMBLY=OFF for native builds.")
endif()

# Native gtest suites use POSIX sockets, GLFW/OpenGL, and other host APIs.
# Keep those off in WebAssembly mode and use the explicit web smoke tests below.
if(SNF_WEB_ASSEMBLY)
    set(SNF_ENABLE_TESTS OFF CACHE BOOL
        "Native tests disabled automatically in WebAssembly mode" FORCE)
endif()

if(SNF_ENABLE_TESTS OR (SNF_WEB_ASSEMBLY AND SNF_ENABLE_WEB_TESTS))
    include(CTest)
endif()

if (SNF_ENABLE_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    )

    FetchContent_MakeAvailable(googletest)
endif()

# ── Library targets ───────────────────────────────────────────────────────────
add_subdirectory(lib/Core)
add_subdirectory(lib/Json)
add_subdirectory(lib/Database)
add_subdirectory(lib/Network)
if(SNF_EXPERIMENTAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/experimental/CMakeLists.txt")
    add_subdirectory(lib/experimental)
endif()

if(NOT SNF_WEB_ASSEMBLY)
    # SNFHttpServer is a development HTTP server; only for native builds.
    add_subdirectory(lib/HttpServer)
    if(SNF_ENABLE_SNMP)
        add_subdirectory(lib/Snmp)
    endif()
endif()

# SNFWebSocket provides non-TLS ws:// client support on all platforms.
# Its server and manual framing backend are native-only.
add_subdirectory(lib/WebSocket)

if(SNF_ENABLE_PROFILING AND NOT SNF_WEB_ASSEMBLY)
    add_subdirectory(lib/Profiler)
    target_compile_definitions(SNFCore PRIVATE SNF_ENABLE_PROFILING)
    target_include_directories(SNFCore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib/Profiler/include")
endif()

# SNFWidgets provides the GLFW+ImGui integration layer.
# On web (SNF_WEB_ASSEMBLY=ON) it uses the Emscripten contrib.glfw3 port and OpenGL ES 3.
# On desktop (SNF_WEB_ASSEMBLY=OFF) it uses system GLFW 3.4+ and OpenGL 3.3 Core Profile.
if(SNF_ENABLE_WIDGETS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/Widgets/CMakeLists.txt")
    add_subdirectory(lib/Widgets)
endif()

if(SNF_ENABLE_STANDALONE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/standalone/CMakeLists.txt")
    add_subdirectory(standalone)
endif()

if(SNF_ENABLE_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

if(SNF_WEB_ASSEMBLY AND SNF_ENABLE_WEB_TESTS)
    add_executable(SNFWebAssemblySmokeTests
        "${CMAKE_CURRENT_SOURCE_DIR}/test/WebAssemblySmokeTests.cpp"
    )
    target_link_libraries(SNFWebAssemblySmokeTests
        PRIVATE
            SNFCore
            SNFNetwork
            SNFWebSocket
    )
    target_compile_features(SNFWebAssemblySmokeTests PRIVATE cxx_std_17)
    target_link_options(SNFWebAssemblySmokeTests PRIVATE
        -sENVIRONMENT=node
        -sEXIT_RUNTIME=1
        -sFORCE_FILESYSTEM=1
    )
    add_test(NAME SNFWebAssemblySmokeTests COMMAND SNFWebAssemblySmokeTests)
endif()

# ── Package config helpers ───────────────────────────────────────────────────
function(snf_install_package_config module_name)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${module_name}Config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${module_name}Config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${module_name}"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${module_name}ConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${module_name}Config.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/${module_name}ConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${module_name}"
    )
endfunction()

# ── Package config: always-on modules ────────────────────────────────────────
snf_install_package_config(SNFCore)
snf_install_package_config(SNFNetwork)
snf_install_package_config(SNFJson)
snf_install_package_config(SNFDatabase)
snf_install_package_config(SNFWebSocket)

# ── Package config: native-only modules ──────────────────────────────────────
if(NOT SNF_WEB_ASSEMBLY)
    snf_install_package_config(SNFHttpServer)

    # Only install SNFSnmp package config if the target exists.
    # This avoids installing a config that references missing export files
    # when net-snmp is unavailable and lib/Snmp returns early.
    if(TARGET SNFSnmp)
        snf_install_package_config(SNFSnmp)
    endif()
endif()

if(SNF_ENABLE_PROFILING AND NOT SNF_WEB_ASSEMBLY)
    snf_install_package_config(SNFProfiler)
endif()
