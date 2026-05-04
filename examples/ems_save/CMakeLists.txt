cmake_minimum_required(VERSION 3.26)

if(EMSCRIPTEN)
    # Web build: only the WASM client.
    add_subdirectory(client)
else()
    # Native build: only the HTTP + WebSocket server.
    add_subdirectory(server)
endif()
