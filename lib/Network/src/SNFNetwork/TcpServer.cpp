#include "SNFNetwork/TcpServer.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
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

TcpServer::TcpServer(Node* parent) : IOEvent(parent)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error | IOEventFlags::HangUp);
}

TcpServer::~TcpServer()
{
    close();

    std::deque<TcpSocket*> pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pending.swap(m_pendingConnections);
    }

    for (TcpSocket* socket : pending) {
        delete socket;
    }
}

bool TcpServer::listen(const std::string& address, std::uint16_t port)
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        loop->post([self = NodePtr<TcpServer>(this), address, port]() {
            if (self) {
                self->listen(address, port);
            }
        });
        return true;
    }

    close();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        failWithErrno("socket() failed", errno);
        return false;
    }

    int reuseAddress = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("setsockopt(SO_REUSEADDR) failed", errorCode);
        return false;
    }

    if (setBlockingMode(fd, false) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("Failed to configure listening socket mode", errorCode);
        return false;
    }

    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(port);

    if (::inet_pton(AF_INET, address.c_str(), &bindAddress.sin_addr) != 1) {
        ::close(fd);
        failWithErrno("Invalid IPv4 listen address", EINVAL);
        return false;
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("bind() failed", errorCode);
        return false;
    }

    if (::listen(fd, static_cast<int>(m_maxPendingConnections)) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("listen() failed", errorCode);
        return false;
    }

    sockaddr_in actualAddress{};
    socklen_t actualAddressLength = sizeof(actualAddress);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&actualAddress), &actualAddressLength) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("getsockname() failed", errorCode);
        return false;
    }

    setDescriptor(fd);
    start();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listening = true;
        m_serverPort = ntohs(actualAddress.sin_port);
        m_serverAddress = address;
    }

    return true;
}

void TcpServer::close()
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        loop->post([self = NodePtr<TcpServer>(this)]() {
            if (self) {
                self->close();
            }
        });
        return;
    }

    stop();
    setDescriptor(-1);

    std::deque<TcpSocket*> pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listening = false;
        m_serverPort = 0;
        m_serverAddress.clear();
        pending.swap(m_pendingConnections);
    }

    for (TcpSocket* socket : pending) {
        delete socket;
    }
}

bool TcpServer::isListening() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_listening;
}

bool TcpServer::hasPendingConnections() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_pendingConnections.empty();
}

TcpSocket* TcpServer::nextPendingConnection()
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pendingConnections.empty()) {
        return nullptr;
    }

    TcpSocket* socket = m_pendingConnections.front();
    m_pendingConnections.pop_front();
    socket->setParent(nullptr);
    return socket;
}

void TcpServer::setMaxPendingConnections(std::size_t maxPendingConnections)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxPendingConnections = maxPendingConnections == 0 ? 1 : maxPendingConnections;
}

std::size_t TcpServer::maxPendingConnections() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxPendingConnections;
}

std::uint16_t TcpServer::serverPort() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_serverPort;
}

std::string TcpServer::serverAddress() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_serverAddress;
}

void TcpServer::handleEvents(std::uint32_t nativeEvents)
{
    IOEvent::handleEvents(nativeEvents);

    if ((nativeEvents & EPOLLIN) != 0u) {
        acceptPendingConnections();
    }

    if ((nativeEvents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0u) {
        close();
    }
}

void TcpServer::acceptPendingConnections()
{
    const int listenFd = descriptor();
    if (listenFd < 0) {
        return;
    }

    while (true) {
        sockaddr_in peerAddress{};
        socklen_t peerLength = sizeof(peerAddress);
        const int acceptedFd = ::accept4(listenFd,
                                         reinterpret_cast<sockaddr*>(&peerAddress),
                                         &peerLength,
                                         SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (acceptedFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }

            failWithErrno("accept4() failed", errno);
            return;
        }

        bool canQueue = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            canQueue = m_pendingConnections.size() < m_maxPendingConnections;
        }

        if (!canQueue) {
            ::close(acceptedFd);
            continue;
        }

        auto* acceptedSocket = new TcpSocket(acceptedFd, false, this);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingConnections.push_back(acceptedSocket);
        }

        emitNewConnection();
    }
}

void TcpServer::emitNewConnection()
{
    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<TcpServer>(this)]() {
            if (self) {
                self->newConnection.emit();
            }
        });
        return;
    }

    newConnection.emit();
}

void TcpServer::emitErrorOccurred(std::string message)
{
    if (EventLoop* loop = ownerEventLoop()) {
        loop->post([self = NodePtr<TcpServer>(this), message = std::move(message)]() {
            if (self) {
                self->errorOccurred.emit(message);
            }
        });
        return;
    }

    errorOccurred.emit(message);
}

void TcpServer::failWithErrno(const std::string& prefix, int errorCode)
{
    emitErrorOccurred(prefix + ": " + std::strerror(errorCode));
}

}  // namespace snf
