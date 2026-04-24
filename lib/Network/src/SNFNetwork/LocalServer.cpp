#include "SNFNetwork/LocalServer.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace snf {

namespace {

constexpr std::size_t kMaxUnixPathLength = sizeof(sockaddr_un::sun_path) - 1;

}  // namespace

LocalServer::LocalServer(Node* parent) : IOEvent(parent)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error | IOEventFlags::HangUp);
}

LocalServer::~LocalServer()
{
    close();

    std::deque<LocalSocket*> pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pending.swap(m_pendingConnections);
    }

    for (LocalSocket* socket : pending) {
        delete socket;
    }
}

bool LocalServer::listen(const std::string& path)
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        loop->post([self = NodePtr<LocalServer>(this), path]() {
            if (self) {
                self->listen(path);
            }
        });
        return true;
    }

    if (path.size() > kMaxUnixPathLength) {
        emitErrorOccurred("UNIX socket path too long (max " + std::to_string(kMaxUnixPathLength) + " bytes): " + path);
        return false;
    }

    close();

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        failWithErrno("socket() failed", errno);
        return false;
    }

    // Remove stale socket file if it exists.
    ::unlink(path.c_str());

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), kMaxUnixPathLength);

    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        const int errorCode = errno;
        ::close(fd);
        failWithErrno("bind() failed", errorCode);
        return false;
    }

    if (::listen(fd, static_cast<int>(m_maxPendingConnections)) < 0) {
        const int errorCode = errno;
        ::unlink(path.c_str());
        ::close(fd);
        failWithErrno("listen() failed", errorCode);
        return false;
    }

    // Set non-blocking after binding so accept4 can use SOCK_NONBLOCK per-client.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    setDescriptor(fd);
    start();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listening = true;
        m_serverPath = path;
    }

    return true;
}

void LocalServer::close()
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        loop->post([self = NodePtr<LocalServer>(this)]() {
            if (self) {
                self->close();
            }
        });
        return;
    }

    stop();
    setDescriptor(-1);

    std::deque<LocalSocket*> pending;
    std::string pathToUnlink;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_listening) {
            return;
        }
        m_listening = false;
        pathToUnlink = std::move(m_serverPath);
        m_serverPath.clear();
        pending.swap(m_pendingConnections);
    }

    for (LocalSocket* socket : pending) {
        delete socket;
    }

    if (!pathToUnlink.empty()) {
        ::unlink(pathToUnlink.c_str());
    }
}

bool LocalServer::isListening() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_listening;
}

bool LocalServer::hasPendingConnections() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_pendingConnections.empty();
}

LocalSocket* LocalServer::nextPendingConnection()
{
    if (EventLoop* loop = ownerEventLoop(); loop && !loop->isInThisThread()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pendingConnections.empty()) {
        return nullptr;
    }

    LocalSocket* socket = m_pendingConnections.front();
    m_pendingConnections.pop_front();
    socket->setParent(nullptr);
    return socket;
}

void LocalServer::setMaxPendingConnections(std::size_t maxPendingConnections)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxPendingConnections = maxPendingConnections == 0 ? 1 : maxPendingConnections;
}

std::size_t LocalServer::maxPendingConnections() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxPendingConnections;
}

std::string LocalServer::serverPath() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_serverPath;
}

void LocalServer::handleEvents(std::uint32_t nativeEvents)
{
    IOEvent::handleEvents(nativeEvents);

    if ((nativeEvents & EPOLLIN) != 0u) {
        acceptPendingConnections();
    }

    if ((nativeEvents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0u) {
        close();
    }
}

void LocalServer::acceptPendingConnections()
{
    const int listenFd = descriptor();
    if (listenFd < 0) {
        return;
    }

    while (true) {
        const int acceptedFd = ::accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

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

        auto* acceptedSocket = new LocalSocket(acceptedFd, false, this);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingConnections.push_back(acceptedSocket);
        }

        emitNewConnection();
    }
}

void LocalServer::emitNewConnection()
{
    newConnection.emit();
}

void LocalServer::emitErrorOccurred(std::string message)
{
    errorOccurred.emit(std::move(message));
}

void LocalServer::failWithErrno(const std::string& prefix, int errorCode)
{
    emitErrorOccurred(prefix + ": " + std::strerror(errorCode));
}

void LocalServer::unlinkPath()
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_serverPath;
    }
    if (!path.empty()) {
        ::unlink(path.c_str());
    }
}

}  // namespace snf
