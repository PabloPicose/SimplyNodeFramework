cmake_minimum_required(VERSION 3.26)

project(SimplyNodeFramework VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SNF_ENABLE_TESTS "Enable tests for SimplyNodeFramework" ${PROJECT_IS_TOP_LEVEL})

if (SNF_ENABLE_TESTS)
    include(CTest)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    )

    FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(lib/Core)
add_subdirectory(lib/Network)

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

