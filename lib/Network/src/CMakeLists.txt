set(SNFNETWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SNFNetwork")

set(SNFNETWORK_SOURCES
    "${SNFNETWORK_DIR}/HostAddress.cpp"
    "${SNFNETWORK_DIR}/IOEvent.cpp"
    "${SNFNETWORK_DIR}/TcpSocket.cpp"
    "${SNFNETWORK_DIR}/TcpServer.cpp"
    PARENT_SCOPE
)

set(SNFNETWORK_HEADERS
    "${SNFNETWORK_DIR}/HostAddress.h"
    "${SNFNETWORK_DIR}/IOEvent.h"
    "${SNFNETWORK_DIR}/TcpSocket.h"
    "${SNFNETWORK_DIR}/TcpServer.h"
    PARENT_SCOPE
)
