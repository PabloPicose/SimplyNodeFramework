#include "SNFNetwork/TcpSocket.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace snf {

namespace {

int setBlockingMode(int fd, bool blocking)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }

    const int nextFlags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return ::fcntl(fd, F_SETFL, nextFlags);
}

}  // namespace

TcpSocket::TcpSocket(bool blocking, Node* parent) : IOEvent(parent), m_blocking(blocking)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error | IOEventFlags::HangUp);
}

TcpSocket::~TcpSocket() { close(); }

void TcpSocket::setBlocking(bool blocking)
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<TcpSocket>(this), blocking]() {
            if (self) {
                self->setBlocking(blocking);
            }
        });
        return;
    }

    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_blocking = blocking;
        fd = descriptor();
    }

    if (fd >= 0 && setBlockingMode(fd, blocking) < 0) {
        failWithErrno("Failed to set socket mode", errno);
    }
}

bool TcpSocket::isBlocking() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_blocking;
}

void TcpSocket::connectToHost(const std::string& host, std::uint16_t port)
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<TcpSocket>(this), host, port]() {
            if (self) {
                self->connectToHost(host, port);
            }
        });
        return;
    }

    close();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        failWithErrno("socket() failed", errno);
        return;
    }

    if (setBlockingMode(fd, isBlocking()) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("Failed to configure socket mode", errorCode);
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(fd);
        failWithErrno("Invalid IPv4 address", EINVAL);
        return;
    }

    setDescriptor(fd);

    const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result == 0) {
        applyConnectedState();
        return;
    }

    if (errno == EINPROGRESS && ! isBlocking()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = TcpSocketState::Connecting;
        }
        updateInterestForState();
        start();
        return;
    }

    failWithErrno("connect() failed", errno);
    close();
}

void TcpSocket::close()
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<TcpSocket>(this)]() {
            if (self) {
                self->close();
            }
        });
        return;
    }

    const bool wasConnected = state() == TcpSocketState::Connected || state() == TcpSocketState::Connecting;

    stop();
    setDescriptor(-1);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = TcpSocketState::Disconnected;
        m_readBuffer.clear();
        m_writeBuffer.clear();
    }

    if (wasConnected) {
        disconnected.emit();
    }
}

std::size_t TcpSocket::write(const std::vector<std::uint8_t>& data)
{
    if (data.empty()) {
        return 0;
    }

    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state != TcpSocketState::Connected && m_state != TcpSocketState::Connecting) {
                return 0;
            }
            m_writeBuffer.insert(m_writeBuffer.end(), data.begin(), data.end());
        }

        loop->post([self = NodePtr<TcpSocket>(this)]() {
            if (self) {
                self->flushPendingWrites();
                self->updateInterestForState();
            }
        });

        return data.size();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != TcpSocketState::Connected && m_state != TcpSocketState::Connecting) {
            return 0;
        }
        m_writeBuffer.insert(m_writeBuffer.end(), data.begin(), data.end());
    }

    flushPendingWrites();
    updateInterestForState();
    return data.size();
}

std::size_t TcpSocket::write(const std::string& data)
{
    return write(std::vector<std::uint8_t>(data.begin(), data.end()));
}

std::vector<std::uint8_t> TcpSocket::readAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::uint8_t> output;
    output.swap(m_readBuffer);
    return output;
}

TcpSocketState TcpSocket::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void TcpSocket::handleEvents(std::uint32_t nativeEvents)
{
    IOEvent::handleEvents(nativeEvents);

    if ((nativeEvents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0u) {
        if (state() == TcpSocketState::Connecting) {
            int socketError = 0;
            socklen_t length = sizeof(socketError);
            const int fd = descriptor();
            if (fd >= 0 && ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 && socketError == 0) {
                applyConnectedState();
                return;
            }
        }

        applyDisconnectedState(true);
        return;
    }

    if (state() == TcpSocketState::Connecting && (nativeEvents & EPOLLOUT) != 0u) {
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        const int fd = descriptor();
        if (fd < 0 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) < 0) {
            failWithErrno("getsockopt(SO_ERROR) failed", errno);
            applyDisconnectedState(true);
            return;
        }

        if (socketError != 0) {
            failWithErrno("connect() async completion failed", socketError);
            applyDisconnectedState(true);
            return;
        }

        applyConnectedState();
    }

    if ((nativeEvents & EPOLLIN) != 0u && state() == TcpSocketState::Connected) {
        handleReadable();
    }

    if ((nativeEvents & EPOLLOUT) != 0u && state() == TcpSocketState::Connected) {
        handleWritable();
    }
}

bool TcpSocket::flushPendingWrites()
{
    const int fd = descriptor();
    if (fd < 0) {
        return false;
    }

    while (true) {
        std::vector<std::uint8_t> localBuffer;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_writeBuffer.empty()) {
                return true;
            }
            localBuffer = m_writeBuffer;
        }

        const ssize_t written = ::send(fd, localBuffer.data(), localBuffer.size(), MSG_NOSIGNAL);
        if (written > 0) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const std::size_t consumed = static_cast<std::size_t>(written);
                m_writeBuffer.erase(m_writeBuffer.begin(),
                                    m_writeBuffer.begin() + static_cast<std::ptrdiff_t>(consumed));
            }
            bytesWritten.emit(static_cast<std::size_t>(written));
            continue;
        }

        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        failWithErrno("send() failed", errno);
        applyDisconnectedState(true);
        return false;
    }
}

void TcpSocket::handleReadable()
{
    const int fd = descriptor();
    if (fd < 0) {
        return;
    }

    bool appended = false;
    std::uint8_t buffer[4096];
    while (true) {
        const ssize_t readBytes = ::recv(fd, buffer, sizeof(buffer), 0);
        if (readBytes > 0) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_readBuffer.insert(m_readBuffer.end(), buffer, buffer + readBytes);
            appended = true;
            continue;
        }

        if (readBytes == 0) {
            applyDisconnectedState(true);
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        failWithErrno("recv() failed", errno);
        applyDisconnectedState(true);
        return;
    }

    if (appended) {
        readyRead.emit();
    }
}

void TcpSocket::handleWritable()
{
    flushPendingWrites();
    updateInterestForState();
}

void TcpSocket::applyConnectedState()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = TcpSocketState::Connected;
    }

    updateInterestForState();
    start();
    connected.emit();

    flushPendingWrites();
    updateInterestForState();
}

void TcpSocket::applyDisconnectedState(bool emitSignal)
{
    const bool shouldEmit = emitSignal && (state() == TcpSocketState::Connected || state() == TcpSocketState::Connecting);

    stop();
    setDescriptor(-1);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = TcpSocketState::Disconnected;
        m_writeBuffer.clear();
    }

    if (shouldEmit) {
        disconnected.emit();
    }
}

void TcpSocket::failWithErrno(const std::string& prefix, int errorCode)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = TcpSocketState::Error;
    }

    errorOccurred.emit(prefix + ": " + std::strerror(errorCode));
}

void TcpSocket::updateInterestForState()
{
    IOEventFlags flags = IOEventFlags::Error | IOEventFlags::HangUp;

    if (state() == TcpSocketState::Connecting) {
        flags |= IOEventFlags::Write;
    }

    if (state() == TcpSocketState::Connected) {
        flags |= IOEventFlags::Read;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (! m_writeBuffer.empty()) {
            flags |= IOEventFlags::Write;
        }
    }

    setInterest(flags);
}

}  // namespace snf
