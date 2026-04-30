set(SNFNETWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SNFNetwork")

set(SNFNETWORK_SOURCES
    "${SNFNETWORK_DIR}/HostAddress.cpp"
)

set(SNFNETWORK_HEADERS
    "${SNFNETWORK_DIR}/ByteArray.h"
    "${SNFNETWORK_DIR}/HostAddress.h"
)

if(NOT SNF_WEB_ASSEMBLY)
    list(APPEND SNFNETWORK_SOURCES
    "${SNFNETWORK_DIR}/IOEvent.cpp"
    "${SNFNETWORK_DIR}/TcpSocket.cpp"
    "${SNFNETWORK_DIR}/TcpServer.cpp"
    "${SNFNETWORK_DIR}/LocalSocket.cpp"
    "${SNFNETWORK_DIR}/LocalServer.cpp"
    "${SNFNETWORK_DIR}/NetworkDatagram.cpp"
    "${SNFNETWORK_DIR}/UdpSocket.cpp"
    )

    list(APPEND SNFNETWORK_HEADERS
    "${SNFNETWORK_DIR}/IOEvent.h"
    "${SNFNETWORK_DIR}/TcpSocket.h"
    "${SNFNETWORK_DIR}/TcpServer.h"
    "${SNFNETWORK_DIR}/LocalSocket.h"
    "${SNFNETWORK_DIR}/LocalServer.h"
    "${SNFNETWORK_DIR}/NetworkDatagram.h"
    "${SNFNETWORK_DIR}/UdpSocket.h"
    )
endif()

set(SNFNETWORK_SOURCES ${SNFNETWORK_SOURCES} PARENT_SCOPE)
set(SNFNETWORK_HEADERS ${SNFNETWORK_HEADERS} PARENT_SCOPE)
