#pragma once
#include "SysMonitor.h"
#include "ProfilerNode.h"
#include "SNFWebSocket/WebSocketServer.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace snf::profiler {

/**
 * @brief WebSocket server that streams profiler data to the dashboard.
 */
class ProfilerServer : public snf::Node {
public:
    /// Creates a server bound to @p port and wired to the profiler sources.
    explicit ProfilerServer(ProfilerNode* profilerNode,
                            SysMonitor*   sysMonitor,
                            snf::Node*    parent = nullptr,
                            uint16_t      port   = 8765);
    ~ProfilerServer() override = default;

    void update() override {}

    /// Returns the listening port.
    uint16_t port() const;

    /// Number of clients currently receiving the stream.
    size_t clientCount() const;

    /// A client with more than @p bytes queued and not yet sent is reading too
    /// slowly: it is closed instead of letting its queue grow without bound.
    void setMaxPendingBytes(size_t bytes);

private:
    void onNewConnection();
    void onClientDisconnected(snf::WebSocket* client);
    void broadcast(const std::string& msg);
    void onSysSample(const SysSample& s);

    struct Client {
        snf::WebSocket*  socket;
        snf::Connection  disconnected;
    };

    snf::WebSocketServer* m_wsServer = nullptr;
    std::vector<Client>   m_clients;
    size_t                m_maxPendingBytes = 16u * 1024u * 1024u;
};

} // namespace snf::profiler
