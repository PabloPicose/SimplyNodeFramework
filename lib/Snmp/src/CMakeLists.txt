set(SNFSNMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SNFSnmp")

set(SNFSNMP_SOURCES
    "${SNFSNMP_DIR}/SnmpOid.cpp"
    "${SNFSNMP_DIR}/SnmpValue.cpp"
    "${SNFSNMP_DIR}/SnmpV3Session.cpp"
    "${SNFSNMP_DIR}/SnmpTrapReceiver.cpp"
)

set(SNFSNMP_HEADERS
    "${SNFSNMP_DIR}/SnmpTypes.h"
    "${SNFSNMP_DIR}/SnmpOid.h"
    "${SNFSNMP_DIR}/SnmpValue.h"
    "${SNFSNMP_DIR}/SnmpVarBind.h"
    "${SNFSNMP_DIR}/SnmpV3Credentials.h"
    "${SNFSNMP_DIR}/SnmpV3Config.h"
    "${SNFSNMP_DIR}/SnmpV3Session.h"
    "${SNFSNMP_DIR}/SnmpTrapInfo.h"
    "${SNFSNMP_DIR}/SnmpTrapReceiver.h"
)

set(SNFSNMP_SOURCES ${SNFSNMP_SOURCES} PARENT_SCOPE)
set(SNFSNMP_HEADERS ${SNFSNMP_HEADERS} PARENT_SCOPE)
