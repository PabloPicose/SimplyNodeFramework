add_executable(httpapp main.cpp)

target_link_libraries(httpapp PRIVATE SNFCore SNFHttpServer SNFNetwork)

set(SNF_HTTPAPP_WEB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../web")
set(SNF_HTTPAPP_WEB_RUNTIME_DIR "$<TARGET_FILE_DIR:httpapp>/web")

add_custom_command(TARGET httpapp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SNF_HTTPAPP_WEB_RUNTIME_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
            "${SNF_HTTPAPP_WEB_SOURCE_DIR}"
            "${SNF_HTTPAPP_WEB_RUNTIME_DIR}"
    COMMENT "Linking standalone/web next to httpapp"
)
