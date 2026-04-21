# SimplyNodeFramework

Modular C++ library with two independent CMake packages:

- `SNFCore`
- `SNFNetwork` (depends on `SNFCore`)

## Using FetchContent (from GitHub)

```cmake
include(FetchContent)

FetchContent_Declare(
	SimplyNodeFramework
	GIT_REPOSITORY https://github.com/<owner>/SimplyNodeFramework.git
	GIT_TAG main
)

FetchContent_MakeAvailable(SimplyNodeFramework)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE SNFCore)
# or:
# target_link_libraries(app PRIVATE SNFNetwork)
```

To avoid building tests when used as a dependency:

```cmake
set(SNF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
```

`SNF_ENABLE_TESTS` defaults to `ON` only when this project is top-level.

## Using find_package after installation

```cmake
find_package(SNFCore CONFIG REQUIRED)
find_package(SNFNetwork CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE SNFNetwork)
```

Available targets:

- Legacy: `SNFCore`, `SNFNetwork`
- Namespaced: `SNFCore::SNFCore`, `SNFNetwork::SNFNetwork`