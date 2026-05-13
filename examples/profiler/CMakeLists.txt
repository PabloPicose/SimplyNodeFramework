cmake_minimum_required(VERSION 3.26)

add_executable(snf_profiler_example profiler_example.cpp)

target_link_libraries(snf_profiler_example PRIVATE SNFProfiler)
