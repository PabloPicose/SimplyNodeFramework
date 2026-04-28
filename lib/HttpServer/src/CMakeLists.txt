set(SNFHTTPSERVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SNFHttpServer")

set(SNFHTTPSERVER_SOURCES
    "${SNFHTTPSERVER_DIR}/HttpRequest.cpp"
    "${SNFHTTPSERVER_DIR}/HttpResponse.cpp"
    "${SNFHTTPSERVER_DIR}/HttpRequestParser.cpp"
    "${SNFHTTPSERVER_DIR}/HttpServer.cpp"
    PARENT_SCOPE
)

set(SNFHTTPSERVER_HEADERS
    "${SNFHTTPSERVER_DIR}/HttpRequest.h"
    "${SNFHTTPSERVER_DIR}/HttpResponse.h"
    "${SNFHTTPSERVER_DIR}/HttpRequestParser.h"
    "${SNFHTTPSERVER_DIR}/HttpServer.h"
    PARENT_SCOPE
)
