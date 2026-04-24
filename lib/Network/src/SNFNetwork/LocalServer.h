#pragma once

#include "SNFNetwork/IOEvent.h"
#include "SNFNetwork/LocalSocket.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace snf {

class LocalServer final : public IOEvent
{
public:
    explicit LocalServer(Node* parent = nullptr);
    ~LocalServer() override;

    bool listen(const std::string& path);
    void close();

    bool isListening() const;
    bool hasPendingConnections() const;
    LocalSocket* nextPendingConnection();

    void setMaxPendingConnections(std::size_t maxPendingConnections);
    std::size_t maxPendingConnections() const;

    std::string serverPath() const;

    Signal<> newConnection;
    Signal<std::string> errorOccurred;

protected:
    void handleEvents(std::uint32_t nativeEvents) override;

private:
    void acceptPendingConnections();
    void emitNewConnection();
    void emitErrorOccurred(std::string message);
    void failWithErrno(const std::string& prefix, int errorCode);
    void unlinkPath();

private:
    mutable std::mutex m_mutex;
    std::deque<LocalSocket*> m_pendingConnections;
    std::size_t m_maxPendingConnections = 30;
    bool m_listening = false;
    std::string m_serverPath;
};

}  // namespace snf
