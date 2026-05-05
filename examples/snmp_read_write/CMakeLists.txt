cmake_minimum_required(VERSION 3.26)

if(NOT TARGET SNFSnmp)
    message(STATUS "Skipping snmp_read_write example — SNFSnmp not available")
    return()
endif()

add_executable(snmp_read_write main.cpp)

target_link_libraries(snmp_read_write
    PRIVATE
        SNFCore
        SNFSnmp
)

target_compile_options(snmp_read_write PRIVATE -Wall -Wextra)
