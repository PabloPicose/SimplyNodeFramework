cmake_minimum_required(VERSION 3.26)

add_executable(ems_save_server main.cpp)

target_link_libraries(ems_save_server
    PRIVATE
        SNFCore
        SNFHttpServer
        SNFNetwork
        SNFWebSocket
)

target_compile_options(ems_save_server PRIVATE -Wall -Wextra)
