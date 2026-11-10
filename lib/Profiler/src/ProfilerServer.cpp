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

size_t ProfilerServer::clientCount() const {
    return static_cast<size_t>(std::count_if(m_clients.begin(), m_clients.end(),
                                             [](const Client& c) { return c.socket->isOpen(); }));
}

void ProfilerServer::setMaxPendingBytes(size_t bytes) {
    m_maxPendingBytes = bytes;
}

void ProfilerServer::onNewConnection() {
    while (m_wsServer->hasPendingConnections()) {
        snf::WebSocket* client = m_wsServer->nextPendingConnection();
        client->setParent(this); // server owns client

        client->sendTextMessage(
            R"({"type":"hello","version":2,"capabilities":["trace","memory","sys"]})");

        auto conn = client->disconnected.connect([this, client]() {
            onClientDisconnected(client);
        });
        m_clients.push_back({client, conn});
    }
}

void ProfilerServer::broadcast(const std::string& msg) {
    for (Client& c : m_clients) {
        if (!c.socket->isOpen())
            continue;  // closing; removed once disconnected fires
        if (c.socket->bytesToWrite() > m_maxPendingBytes) {
            std::fprintf(stderr,
                "[SNFProfiler] closing dashboard client %s:%u: %zu bytes not sent (reads too slowly)\n",
                c.socket->peerAddress().toString().c_str(),
                static_cast<unsigned>(c.socket->peerPort()),
                c.socket->bytesToWrite());
            c.socket->close();
            continue;
        }
        c.socket->sendTextMessage(msg);
    }
}

void ProfilerServer::onClientDisconnected(snf::WebSocket* client) {
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                           [client](const Client& c) { return c.socket == client; });
    if (it != m_clients.end()) {
        it->disconnected.disconnect();
        m_clients.erase(it);
    }
    client->deleteLater();
}

void ProfilerServer::onSysSample(const SysSample& s) {
    broadcast(detail::serializeSysSample(s));
}

} // namespace snf::profiler
