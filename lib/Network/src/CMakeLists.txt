set(SNFNETWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SNFNetwork")

set(SNFNETWORK_SOURCES
    "${SNFNETWORK_DIR}/HostAddress.cpp"
    "${SNFNETWORK_DIR}/IOEvent.cpp"
    "${SNFNETWORK_DIR}/TcpSocket.cpp"
    "${SNFNETWORK_DIR}/TcpServer.cpp"
    "${SNFNETWORK_DIR}/LocalSocket.cpp"
    "${SNFNETWORK_DIR}/LocalServer.cpp"
    "${SNFNETWORK_DIR}/NetworkDatagram.cpp"
    "${SNFNETWORK_DIR}/UdpSocket.cpp"
    PARENT_SCOPE
)

set(SNFNETWORK_HEADERS
    "${SNFNETWORK_DIR}/HostAddress.h"
    "${SNFNETWORK_DIR}/IOEvent.h"
    "${SNFNETWORK_DIR}/TcpSocket.h"
    "${SNFNETWORK_DIR}/TcpServer.h"
    "${SNFNETWORK_DIR}/LocalSocket.h"
    "${SNFNETWORK_DIR}/LocalServer.h"
    "${SNFNETWORK_DIR}/NetworkDatagram.h"
    "${SNFNETWORK_DIR}/UdpSocket.h"
    PARENT_SCOPE
)
