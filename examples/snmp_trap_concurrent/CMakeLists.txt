cmake_minimum_required(VERSION 3.26)

if(NOT TARGET SNFSnmp)
    message(STATUS "Skipping snmp_trap_concurrent example — SNFSnmp not available")
    return()
endif()

add_executable(snmp_trap_concurrent main.cpp)

target_link_libraries(snmp_trap_concurrent
    PRIVATE
        SNFCore
        SNFSnmp
)

target_compile_options(snmp_trap_concurrent PRIVATE -Wall -Wextra)
