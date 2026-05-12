#include "Profiler/ProfilerServer.h"
#include "SNFNetwork/HostAddress.h"
#include "SNFWebSocket/WebSocket.h"
#include <algorithm>
#include <cstdio>

namespace snf::profiler {

ProfilerServer::ProfilerServer(ProfilerNode* profilerNode, SysMonitor* sysMonitor,
                               snf::Node* parent, uint16_t port)
    : snf::Node(parent)
{
    m_wsServer = new snf::WebSocketServer(this);
    m_wsServer->newConnection.connect([this]() { onNewConnection(); });
    if (!m_wsServer->listen(snf::HostAddress::AnyIPv4, port) && port != 0) {
        // Requested port is in use (common in parallel test runs); fall back to
        // an OS-assigned ephemeral port so callers can still use server->port().
        m_wsServer->listen(snf::HostAddress::AnyIPv4, 0);
    }

    // Connect to profilerNode's broadcastMessage (Queued: profilerNode is on pool thread)
    profilerNode->broadcastMessage.connect(
        snf::NodePtr<ProfilerServer>(this),
        [](ProfilerServer& self, std::string msg) { self.broadcast(msg); },
        snf::ConnectionType::Queued);

    // Connect to sysMonitor's sampleReady (Queued: sysMonitor is on pool thread)
    sysMonitor->sampleReady.connect(
        snf::NodePtr<ProfilerServer>(this),
        [](ProfilerServer& self, SysSample s) { self.onSysSample(s); },
        snf::ConnectionType::Queued);
}

uint16_t ProfilerServer::port() const {
    return m_wsServer->serverPort();
}

void ProfilerServer::onNewConnection() {
    while (m_wsServer->hasPendingConnections()) {
        snf::WebSocket* client = m_wsServer->nextPendingConnection();
        client->setParent(this); // server owns client

        client->sendTextMessage(
            R"({"type":"hello","version":1,"capabilities":["trace","memory","sys"]})");

        auto discConn = client->disconnected.connect([this, client]() {
            onClientDisconnected(client);
        });
        m_clientConns.push_back(discConn);
        m_clients.push_back(client);
    }
}

void ProfilerServer::broadcast(const std::string& msg) {
    for (auto it = m_clients.begin(); it != m_clients.end(); ) {
        if ((*it)->isOpen()) {
            (*it)->sendTextMessage(msg);
            ++it;
        } else {
            it = m_clients.erase(it);
        }
    }
}

void ProfilerServer::onClientDisconnected(snf::WebSocket* client) {
    m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), client), m_clients.end());
    client->deleteLater();
}

void ProfilerServer::onSysSample(const SysSample& s) {
    broadcast(serializeSysSample(s));
}

std::string ProfilerServer::serializeSysSample(const SysSample& s) const {
    std::string cpu_arr = "[";
    for (size_t i = 0; i < s.cpu_usage.size(); ++i) {
        if (i) cpu_arr += ',';
        char tmp[16];
        std::snprintf(tmp, sizeof(tmp), "%.4f", s.cpu_usage[i]);
        cpu_arr += tmp;
    }
    cpu_arr += "]";

    std::string net_obj = "{";
    bool first = true;
    for (auto& [iface, stat] : s.net) {
        if (!first) net_obj += ',';
        char tmp[256];
        std::snprintf(tmp, sizeof(tmp),
            R"("%s":{"rx_Bps":%llu,"tx_Bps":%llu})",
            iface.c_str(), (unsigned long long)stat.rx_Bps, (unsigned long long)stat.tx_Bps);
        net_obj += tmp;
        first = false;
    }
    net_obj += "}";

    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"sys","ts":%llu,"cpu":%s,"ram_used":%llu,"ram_free":%llu,"net":%s})",
        (unsigned long long)s.timestamp_ns,
        cpu_arr.c_str(),
        (unsigned long long)s.ram_used_bytes,
        (unsigned long long)s.ram_free_bytes,
        net_obj.c_str());
    return buf;
}

} // namespace snf::profiler
