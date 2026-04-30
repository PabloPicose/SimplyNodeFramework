#include "SNFNetwork/TcpSocket.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"
#include "SNFNetwork/HostAddress.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
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

socklen_t socketAddressLength(const sockaddr_storage& address)
{
    if (address.ss_family == AF_INET6) {
        return static_cast<socklen_t>(sizeof(sockaddr_in6));
    }
    return static_cast<socklen_t>(sizeof(sockaddr_in));
}

bool peerEndpoint(int fd, std::string& address, std::uint16_t& port)
{
    address.clear();
    port = 0;

    if (fd < 0) {
        return false;
    }

    sockaddr_storage storage{};
    socklen_t storageLength = sizeof(storage);
    if (::getpeername(fd, reinterpret_cast<sockaddr*>(&storage), &storageLength) != 0) {
        return false;
    }

    char buffer[INET6_ADDRSTRLEN] = {};
    if (storage.ss_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&storage);
        if (! ::inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer))) {
            return false;
        }
        address = buffer;
        port = ntohs(ipv4->sin_port);
        return true;
    }

    if (storage.ss_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&storage);
        if (! ::inet_ntop(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer))) {
            return false;
        }
        address = buffer;
        port = ntohs(ipv6->sin6_port);
        return true;
    }

    return false;
}

}  // namespace

TcpSocket::TcpSocket(bool blocking, Node* parent) : IOEvent(parent), m_blocking(blocking)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error | IOEventFlags::HangUp);
}

TcpSocket::TcpSocket(int adoptedFd, bool blocking, Node* parent) : IOEvent(parent), m_blocking(blocking)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error | IOEventFlags::HangUp);
    adoptConnectedDescriptor(adoptedFd);
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

void TcpSocket::connectToHost(const HostAddress& hostAddress, std::uint16_t port)
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<TcpSocket>(this), hostAddress, port]() {
            if (self) {
                self->connectToHost(hostAddress, port);
            }
        });
        return;
    }

    close();

    std::vector<sockaddr_storage> candidates;
    std::string resolveError;
    if (!hostAddress.resolve(port, HostResolveMode::Connect, candidates, resolveError)) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = TcpSocketState::Error;
        }
        emitErrorOccurred("Failed to resolve host '" + hostAddress.host() + "': " + resolveError);
        return;
    }

    int lastError = EHOSTUNREACH;

    for (const sockaddr_storage& candidate : candidates) {
        const sockaddr* address = reinterpret_cast<const sockaddr*>(&candidate);
        const int fd = ::socket(address->sa_family, SOCK_STREAM, 0);
        if (fd < 0) {
            lastError = errno;
            continue;
        }

        if (setBlockingMode(fd, isBlocking()) < 0) {
            lastError = errno;
            ::close(fd);
            continue;
        }

        setDescriptor(fd);

        const int result = ::connect(fd, address, socketAddressLength(candidate));
        if (result == 0) {
            applyConnectedState();
            return;
        }

        if (errno == EINPROGRESS && !isBlocking()) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_state = TcpSocketState::Connecting;
            }
            updateInterestForState();
            start();
            return;
        }

        lastError = errno;
        setDescriptor(-1);
    }

    failWithErrno("connect() failed", lastError);
    close();
}

void TcpSocket::connectToHost(const std::string& host, std::uint16_t port)
{
    connectToHost(HostAddress(host), port);
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

    const bool shouldEmit = transitionToDisconnected(true);
    stop();
    setDescriptor(-1);

    if (shouldEmit) {
        emitDisconnected();
    }
}

std::size_t TcpSocket::write(ByteArray data)
{
    if (data.remainingSize() == 0) {
        return 0;
    }

    const std::size_t acceptedBytes = data.remainingSize();

    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        const bool canQueue = [this]() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_state == TcpSocketState::Connected || m_state == TcpSocketState::Connecting;
        }();

        if (! canQueue) {
            return 0;
        }

        loop->post([self = NodePtr<TcpSocket>(this), pending = std::move(data)]() mutable {
            if (self) {
                self->write(std::move(pending));
            }
        });
        return acceptedBytes;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != TcpSocketState::Connected && m_state != TcpSocketState::Connecting) {
            return 0;
        }
        m_writeQueue.push_back(std::move(data));
    }

    flushPendingWrites();
    updateInterestForState();
    return acceptedBytes;
}

std::size_t TcpSocket::write(const std::vector<std::uint8_t>& data)
{
    return write(ByteArray(data));
}

std::size_t TcpSocket::write(const std::string& data)
{
    return write(ByteArray(data));
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

HostAddress TcpSocket::peerAddress() const
{
    std::string address;
    std::uint16_t port = 0;
    if (! peerEndpoint(descriptor(), address, port)) {
        return HostAddress();
    }

    return HostAddress(address);
}

std::uint16_t TcpSocket::peerPort() const
{
    std::string address;
    std::uint16_t port = 0;
    if (! peerEndpoint(descriptor(), address, port)) {
        return 0;
    }

    return port;
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
        const std::byte* remainingData = nullptr;
        std::size_t remainingSize = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (! m_writeQueue.empty() && m_writeQueue.front().fullyConsumed()) {
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
                if (! m_writeQueue.empty()) {
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
        emitReadyRead();
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
    emitConnected();

    flushPendingWrites();
    updateInterestForState();
}

void TcpSocket::applyDisconnectedState(bool emitSignal)
{
    const bool shouldEmit = transitionToDisconnected(emitSignal);

    stop();
    setDescriptor(-1);

    if (shouldEmit) {
        emitDisconnected();
    }
}

bool TcpSocket::transitionToDisconnected(bool emitSignal)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const bool wasConnected = m_state == TcpSocketState::Connected || m_state == TcpSocketState::Connecting;
    m_state = TcpSocketState::Disconnected;
    m_readBuffer.clear();
    m_writeQueue.clear();

    return emitSignal && wasConnected;
}

void TcpSocket::failWithErrno(const std::string& prefix, int errorCode)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = TcpSocketState::Error;
    }

    emitErrorOccurred(prefix + ": " + std::strerror(errorCode));
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
        if (! m_writeQueue.empty()) {
            flags |= IOEventFlags::Write;
        }
    }

    setInterest(flags);
}

void TcpSocket::emitConnected()
{
    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<TcpSocket>(this)]() {
            if (self) {
                self->connected.emit();
            }
        });
        return;
    }

    connected.emit();
}

void TcpSocket::emitDisconnected()
{
    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<TcpSocket>(this)]() {
            if (self) {
                self->disconnected.emit();
            }
        });
        return;
    }

    disconnected.emit();
}

void TcpSocket::emitReadyRead()
{
    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<TcpSocket>(this)]() {
            if (self) {
                self->readyRead.emit();
            }
        });
        return;
    }

    readyRead.emit();
}

void TcpSocket::emitBytesWritten(std::size_t written)
{
    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<TcpSocket>(this), written]() {
            if (self) {
                self->bytesWritten.emit(written);
            }
        });
        return;
    }

    bytesWritten.emit(written);
}

void TcpSocket::emitErrorOccurred(std::string message)
{
    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<TcpSocket>(this), message = std::move(message)]() {
            if (self) {
                self->errorOccurred.emit(message);
            }
        });
        return;
    }

    errorOccurred.emit(message);
}

void TcpSocket::adoptConnectedDescriptor(int fd)
{
    if (fd < 0) {
        failWithErrno("Invalid adopted descriptor", EINVAL);
        return;
    }

    if (setBlockingMode(fd, isBlocking()) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("Failed to configure adopted socket mode", errorCode);
        return;
    }

    setDescriptor(fd);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = TcpSocketState::Connected;
    }

    updateInterestForState();
    start();
}

}  // namespace snf
