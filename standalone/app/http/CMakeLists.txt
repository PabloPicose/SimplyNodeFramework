add_executable(httpapp main.cpp)

target_link_libraries(httpapp SNFCore::SNFCore SNFHttpServer::SNFHttpServer)

target_compile_definitions(httpapp PRIVATE
	SNF_HTTPAPP_DEFAULT_WEB_ROOT="${CMAKE_BINARY_DIR}/web"
)
