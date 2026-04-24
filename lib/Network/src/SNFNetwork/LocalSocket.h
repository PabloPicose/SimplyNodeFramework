#pragma once

#include "SNFNetwork/IOEvent.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace snf {

enum class LocalSocketState {
    Disconnected,
    Connecting,
    Connected,
    Error,
};

class LocalSocket final : public IOEvent
{
public:
    explicit LocalSocket(bool blocking = false, Node* parent = nullptr);
    explicit LocalSocket(int adoptedFd, bool blocking = false, Node* parent = nullptr);
    ~LocalSocket() override;

    void setBlocking(bool blocking);
    bool isBlocking() const;

    void connectToPath(const std::string& path);
    void close();

    std::size_t write(const std::vector<std::uint8_t>& data);
    std::size_t write(const std::string& data);
    std::vector<std::uint8_t> readAll();

    LocalSocketState state() const;

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
    bool transitionToDisconnected(bool emitSignal);
    void failWithErrno(const std::string& prefix, int errorCode);
    void updateInterestForState();
    void emitConnected();
    void emitDisconnected();
    void emitReadyRead();
    void emitBytesWritten(std::size_t written);
    void emitErrorOccurred(std::string message);
    void adoptConnectedDescriptor(int fd);

private:
    mutable std::mutex m_mutex;
    bool m_blocking = false;
    LocalSocketState m_state = LocalSocketState::Disconnected;
    std::vector<std::uint8_t> m_readBuffer;
    std::vector<std::uint8_t> m_writeBuffer;
};

}  // namespace snf
