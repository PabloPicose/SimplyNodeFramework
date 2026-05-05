cmake_minimum_required(VERSION 3.26)

add_executable(snf_core_thread_affinity_conn_example
    thread_affinity_conn.cpp
)

target_link_libraries(snf_core_thread_affinity_conn_example PRIVATE SNFCore)
