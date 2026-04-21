cmake_minimum_required(VERSION 3.26)

project(SimplyNodeFramework)

option(ENABLE_TEST "Enable testing" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (ENABLE_TEST)
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

