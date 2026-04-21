#pragma once

#include "SNFNetwork/HostAddress.h"
#include "SNFNetwork/IOEvent.h"
#include "SNFNetwork/TcpSocket.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace snf {

class TcpServer final : public IOEvent
{
public:
    explicit TcpServer(Node* parent = nullptr);
    ~TcpServer() override;

    bool listen(const HostAddress& address, std::uint16_t port);
    bool listen(const std::string& address, std::uint16_t port);
    void close();

    bool isListening() const;
    bool hasPendingConnections() const;
    TcpSocket* nextPendingConnection();

    void setMaxPendingConnections(std::size_t maxPendingConnections);
    std::size_t maxPendingConnections() const;

    std::uint16_t serverPort() const;
    std::string serverAddress() const;

    Signal<> newConnection;
    Signal<std::string> errorOccurred;

protected:
    void handleEvents(std::uint32_t nativeEvents) override;

private:
    void acceptPendingConnections();
    void emitNewConnection();
    void emitErrorOccurred(std::string message);
    void failWithErrno(const std::string& prefix, int errorCode);

private:
    mutable std::mutex m_mutex;
    std::deque<TcpSocket*> m_pendingConnections;
    std::size_t m_maxPendingConnections = 30;
    bool m_listening = false;
    std::uint16_t m_serverPort = 0;
    std::string m_serverAddress;
};

}  // namespace snf
