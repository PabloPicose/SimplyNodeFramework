#include "SNFNetwork/LocalSocket.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

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

constexpr std::size_t kMaxUnixPathLength = sizeof(sockaddr_un::sun_path) - 1;

}  // namespace

LocalSocket::LocalSocket(bool blocking, Node* parent) : IOEvent(parent), m_blocking(blocking)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error | IOEventFlags::HangUp);
}

LocalSocket::LocalSocket(int adoptedFd, bool blocking, Node* parent) : IOEvent(parent), m_blocking(blocking)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error | IOEventFlags::HangUp);
    adoptConnectedDescriptor(adoptedFd);
}

LocalSocket::~LocalSocket() { close(); }

void LocalSocket::setBlocking(bool blocking)
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        loop->post([self = NodePtr<LocalSocket>(this), blocking]() {
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

bool LocalSocket::isBlocking() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_blocking;
}

void LocalSocket::connectToPath(const std::string& path)
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        loop->post([self = NodePtr<LocalSocket>(this), path]() {
            if (self) {
                self->connectToPath(path);
            }
        });
        return;
    }

    if (path.size() > kMaxUnixPathLength) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = LocalSocketState::Error;
        }
        emitErrorOccurred("UNIX socket path too long (max " + std::to_string(kMaxUnixPathLength) + " bytes): " + path);
        return;
    }

    close();

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        failWithErrno("socket() failed", errno);
        return;
    }

    if (setBlockingMode(fd, isBlocking()) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("Failed to set socket mode", errorCode);
        return;
    }

    setDescriptor(fd);

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), kMaxUnixPathLength);

    const int result = ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == 0) {
        applyConnectedState();
        return;
    }

    if (errno == EINPROGRESS && !isBlocking()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = LocalSocketState::Connecting;
        }
        updateInterestForState();
        start();
        return;
    }

    const int errorCode = errno;
    setDescriptor(-1);
    failWithErrno("connect() failed", errorCode);
}

void LocalSocket::close()
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        loop->post([self = NodePtr<LocalSocket>(this)]() {
            if (self) {
                self->close();
            }
        });
        return;
    }

    const bool shouldEmit = transitionToDisconnected(true);
    stop();
    setDescriptor(-1);

    if (shouldEmit) {
        emitDisconnected();
    }
}

std::size_t LocalSocket::write(ByteArray data)
{
    if (data.remainingSize() == 0) {
        return 0;
    }

    const std::size_t acceptedBytes = data.remainingSize();

    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        const bool canQueue = [this]() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_state == LocalSocketState::Connected || m_state == LocalSocketState::Connecting;
        }();

        if (!canQueue) {
            return 0;
        }

        loop->post([self = NodePtr<LocalSocket>(this), pending = std::move(data)]() mutable {
            if (self) {
                self->write(std::move(pending));
            }
        });
        return acceptedBytes;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != LocalSocketState::Connected && m_state != LocalSocketState::Connecting) {
            return 0;
        }
        m_writeQueue.push_back(std::move(data));
    }

    flushPendingWrites();
    updateInterestForState();
    return acceptedBytes;
}

std::size_t LocalSocket::write(const std::vector<std::uint8_t>& data)
{
    return write(ByteArray(data));
}

std::size_t LocalSocket::write(const std::string& data)
{
    return write(ByteArray(data));
}

std::vector<std::uint8_t> LocalSocket::readAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::uint8_t> output;
    output.swap(m_readBuffer);
    return output;
}

LocalSocketState LocalSocket::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void LocalSocket::handleEvents(std::uint32_t nativeEvents)
{
    IOEvent::handleEvents(nativeEvents);

    if ((nativeEvents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0u) {
        if (state() == LocalSocketState::Connecting) {
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

    if (state() == LocalSocketState::Connecting && (nativeEvents & EPOLLOUT) != 0u) {
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

    if ((nativeEvents & EPOLLIN) != 0u && state() == LocalSocketState::Connected) {
        handleReadable();
    }

    if ((nativeEvents & EPOLLOUT) != 0u && state() == LocalSocketState::Connected) {
        handleWritable();
    }
}

bool LocalSocket::flushPendingWrites()
{
    const int fd = descriptor();
    if (fd < 0) {
        return false;
    }

    while (true) {
        const std::byte* remainingData = nullptr;
        std::size_t remainingSize = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (!m_writeQueue.empty() && m_writeQueue.front().fullyConsumed()) {
                m_writeQueue.pop_front();
            }

            if (m_writeQueue.empty()) {
                return true;
            }

            remainingData = m_writeQueue.front().remainingData();
            remainingSize = m_writeQueue.front().remainingSize();
        }

        const ssize_t written = ::send(fd, remainingData, remainingSize, MSG_NOSIGNAL);
        if (written > 0) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const std::size_t consumed = static_cast<std::size_t>(written);
                if (!m_writeQueue.empty()) {
                    m_writeQueue.front().advance(consumed);
                    if (m_writeQueue.front().fullyConsumed()) {
                        m_writeQueue.pop_front();
                    }
                }
            }
            emitBytesWritten(static_cast<std::size_t>(written));
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

void LocalSocket::handleReadable()
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
        emitReadyRead();
    }
}

void LocalSocket::handleWritable()
{
    flushPendingWrites();
    updateInterestForState();
}

void LocalSocket::applyConnectedState()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = LocalSocketState::Connected;
    }

    updateInterestForState();
    start();
    emitConnected();

    flushPendingWrites();
    updateInterestForState();
}

void LocalSocket::applyDisconnectedState(bool emitSignal)
{
    const bool shouldEmit = transitionToDisconnected(emitSignal);

    stop();
    setDescriptor(-1);

    if (shouldEmit) {
        emitDisconnected();
    }
}

bool LocalSocket::transitionToDisconnected(bool emitSignal)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const bool wasConnected =
        m_state == LocalSocketState::Connected || m_state == LocalSocketState::Connecting;
    m_state = LocalSocketState::Disconnected;
    m_readBuffer.clear();
    m_writeQueue.clear();

    return emitSignal && wasConnected;
}

void LocalSocket::failWithErrno(const std::string& prefix, int errorCode)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = LocalSocketState::Error;
    }

    emitErrorOccurred(prefix + ": " + std::strerror(errorCode));
}

void LocalSocket::updateInterestForState()
{
    IOEventFlags flags = IOEventFlags::Error | IOEventFlags::HangUp;

    const LocalSocketState currentState = state();

    if (currentState == LocalSocketState::Connected || currentState == LocalSocketState::Connecting) {
        flags |= IOEventFlags::Read;
    }

    const bool hasPendingWrites = [this]() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_writeQueue.empty();
    }();

    if (hasPendingWrites || currentState == LocalSocketState::Connecting) {
        flags |= IOEventFlags::Write;
    }

    setInterest(flags);
}

void LocalSocket::emitConnected()
{
    connected.emit();
}

void LocalSocket::emitDisconnected()
{
    disconnected.emit();
}

void LocalSocket::emitReadyRead()
{
    readyRead.emit();
}

void LocalSocket::emitBytesWritten(std::size_t written)
{
    bytesWritten.emit(written);
}

void LocalSocket::emitErrorOccurred(std::string message)
{
    errorOccurred.emit(std::move(message));
}

void LocalSocket::adoptConnectedDescriptor(int fd)
{
    if (fd < 0) {
        return;
    }

    if (setBlockingMode(fd, isBlocking()) < 0) {
        failWithErrno("Failed to set socket mode on adopted fd", errno);
        ::close(fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = LocalSocketState::Connected;
    }

    setDescriptor(fd);
    updateInterestForState();
    start();
}

}  // namespace snf
