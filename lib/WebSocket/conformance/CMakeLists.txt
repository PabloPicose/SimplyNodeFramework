# Drivers for the Autobahn|Testsuite (run via docker, see run_autobahn.sh).
add_executable(SNFWebSocketEchoServer EchoServer.cpp)
target_link_libraries(SNFWebSocketEchoServer PRIVATE SNFWebSocket)

add_executable(SNFWebSocketAutobahnClient AutobahnClient.cpp)
target_link_libraries(SNFWebSocketAutobahnClient PRIVATE SNFWebSocket)
