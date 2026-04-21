#pragma once

#include "SNFNetwork/IOEvent.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace snf {

enum class TcpSocketState {
    Disconnected,
    Connecting,
    Connected,
    Error,
};

class TcpSocket final : public IOEvent
{
public:
    explicit TcpSocket(bool blocking = false, Node* parent = nullptr);
    ~TcpSocket() override;

    void setBlocking(bool blocking);
    bool isBlocking() const;

    void connectToHost(const std::string& host, std::uint16_t port);
    void close();

    std::size_t write(const std::vector<std::uint8_t>& data);
    std::size_t write(const std::string& data);
    std::vector<std::uint8_t> readAll();

    TcpSocketState state() const;

    Signal<> connected;
    Signal<> disconnected;
    Signal<> readyRead;
    Signal<std::size_t> bytesWritten;
    Signal<std::string> errorOccurred;

protected:
    void handleEvents(std::uint32_t nativeEvents) override;

private:
    bool flushPendingWrites();
    void handleReadable();
    void handleWritable();
    void applyConnectedState();
    void applyDisconnectedState(bool emitSignal);
    void failWithErrno(const std::string& prefix, int errorCode);
    void updateInterestForState();

private:
    mutable std::mutex m_mutex;
    bool m_blocking = false;
    TcpSocketState m_state = TcpSocketState::Disconnected;
    std::vector<std::uint8_t> m_readBuffer;
    std::vector<std::uint8_t> m_writeBuffer;
};

}  // namespace snf
