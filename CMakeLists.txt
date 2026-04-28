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

# Tests cannot run inside a browser; force them off in web mode.
if(SNF_WEB_ASSEMBLY)
    set(SNF_ENABLE_TESTS OFF CACHE BOOL
        "Tests disabled automatically in WebAssembly mode" FORCE)
endif()

option(SNF_ENABLE_TESTS "Enable tests for SimplyNodeFramework" ${PROJECT_IS_TOP_LEVEL})
option(SNF_ENABLE_STANDALONE "Build the in-tree standalone examples" ${PROJECT_IS_TOP_LEVEL})

if (SNF_ENABLE_TESTS)
    include(CTest)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    )

    FetchContent_MakeAvailable(googletest)
endif()

# ── Library targets ───────────────────────────────────────────────────────────
add_subdirectory(lib/Core)
add_subdirectory(lib/Network)

if(NOT SNF_WEB_ASSEMBLY)
    # SNFHttpServer is a development HTTP server; only for native builds.
    add_subdirectory(lib/HttpServer)
endif()

# SNFWebSocket provides non-TLS ws:// client support on all platforms.
# Its server and manual framing backend are native-only.
add_subdirectory(lib/WebSocket)

# SNFWidgets provides the GLFW+ImGui integration layer.
# On web (SNF_WEB_ASSEMBLY=ON) it uses the Emscripten contrib.glfw3 port and OpenGL ES 3.
# On desktop (SNF_WEB_ASSEMBLY=OFF) it uses system GLFW 3.4+ and OpenGL 3.3 Core Profile.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/Widgets/CMakeLists.txt")
    add_subdirectory(lib/Widgets)
endif()

if(SNF_ENABLE_STANDALONE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/standalone/CMakeLists.txt")
    add_subdirectory(standalone)
endif()

# ── Package config: SNFCore (always) ─────────────────────────────────────────
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SNFCoreConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SNFCoreConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFCore"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SNFCoreConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/SNFCoreConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/SNFCoreConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFCore"
)

# ── Package config: SNFNetwork ───────────────────────────────────────────────
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SNFNetworkConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/SNFNetworkConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFNetwork"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/SNFNetworkConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/SNFNetworkConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/SNFNetworkConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFNetwork"
    )

if(NOT SNF_WEB_ASSEMBLY)
    # ── Package config: SNFHttpServer (native Linux only) ────────────────────────────
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SNFHttpServerConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/SNFHttpServerConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFHttpServer"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/SNFHttpServerConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/SNFHttpServerConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/SNFHttpServerConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFHttpServer"
    )
endif()

# ── Package config: SNFWebSocket ─────────────────────────────────────────────
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SNFWebSocketConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/SNFWebSocketConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFWebSocket"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/SNFWebSocketConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/SNFWebSocketConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/SNFWebSocketConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SNFWebSocket"
    )
