#include "Profiler/ProfilerServer.h"
#include "Profiler/Serialization.h"
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
            R"({"type":"hello","version":2,"capabilities":["trace","memory","sys"]})");

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
    broadcast(detail::serializeSysSample(s));
}

} // namespace snf::profiler
