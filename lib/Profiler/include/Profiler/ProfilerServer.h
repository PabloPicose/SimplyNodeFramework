#pragma once
#include "SysMonitor.h"
#include "ProfilerNode.h"
#include "SNFWebSocket/WebSocketServer.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include <cstdint>
#include <string>
#include <vector>

namespace snf::profiler {

class ProfilerServer : public snf::Node {
public:
    explicit ProfilerServer(ProfilerNode* profilerNode,
                            SysMonitor*   sysMonitor,
                            snf::Node*    parent = nullptr,
                            uint16_t      port   = 8765);
    ~ProfilerServer() override = default;

    void update() override {}

    uint16_t port() const;

private:
    void onNewConnection();
    void onClientDisconnected(snf::WebSocket* client);
    void broadcast(const std::string& msg);
    void onSysSample(const SysSample& s);

    std::string serializeSysSample(const SysSample& s) const;

    snf::WebSocketServer*        m_wsServer = nullptr;
    std::vector<snf::WebSocket*> m_clients;
    std::vector<snf::Connection> m_clientConns;
};

} // namespace snf::profiler
