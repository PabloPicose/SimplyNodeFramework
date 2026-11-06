#include "SNFNetwork/UdpSocket.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/Executor.h"
#include "SNFCore/NodePtr.h"
#include "SNFNetwork/HostAddress.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>

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

std::string addressToString(const sockaddr_storage& addr)
{
    char buffer[INET6_ADDRSTRLEN];
    std::memset(buffer, 0, sizeof(buffer));

    if (addr.ss_family == AF_INET) {
        const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(&addr);
        if (::inet_ntop(AF_INET, &sin->sin_addr, buffer, sizeof(buffer))) {
            return std::string(buffer);
        }
    } else if (addr.ss_family == AF_INET6) {
        const sockaddr_in6* sin6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        if (::inet_ntop(AF_INET6, &sin6->sin6_addr, buffer, sizeof(buffer))) {
            return std::string(buffer);
        }
    }

    return {};
}

std::uint16_t addressToPort(const sockaddr_storage& addr)
{
    if (addr.ss_family == AF_INET) {
        const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(&addr);
        return ::ntohs(sin->sin_port);
    }

    if (addr.ss_family == AF_INET6) {
        const sockaddr_in6* sin6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        return ::ntohs(sin6->sin6_port);
    }

    return 0;
}

template <typename Fn>
bool invokeOnOwnerThreadAndWait(Executor* owner, Fn&& fn, bool fallback = false)
{
    if (! owner) {
        return fallback;
    }

    // Shared with the posted task, which may outlive this call: if the owner
    // does not pick the task up in time we return and the task, when it
    // eventually runs, must not touch this stack frame.
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        bool started = false;
        bool done = false;
        bool abandoned = false;
        bool result = false;
    };
    auto state = std::make_shared<State>();

    owner->post([state, fn = std::forward<Fn>(fn)]() mutable {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->abandoned) {
                // The caller already gave up and reported failure.
                return;
            }
            state->started = true;
        }
        const bool result = fn();
        std::lock_guard<std::mutex> lock(state->mutex);
        state->result = result;
        state->done = true;
        state->cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(state->mutex);
    if (! state->cv.wait_for(lock, std::chrono::seconds(2), [&]() { return state->started || state->done; })) {
        // Not started in time: drop the request so it is never applied later.
        state->abandoned = true;
        return fallback;
    }
    // Already running on the owner thread: wait for the real result.
    state->cv.wait(lock, [&]() { return state->done; });
    return state->result;
}

}  // namespace

UdpSocket::UdpSocket(bool blocking, Node* parent) : IOEvent(parent), m_blocking(blocking)
{
    setInterest(IOEventFlags::Read | IOEventFlags::Error);
}

UdpSocket::~UdpSocket() { transitionToUnbound(); }

void UdpSocket::setBlocking(bool blocking)
{
    if (Executor* owner = ownerExecutor(); owner && !owner->isCurrent()) {
        owner->post([self = NodePtr<UdpSocket>(this), blocking]() {
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

bool UdpSocket::isBlocking() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_blocking;
}

void UdpSocket::setReuseAddress(bool reuse)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_reuseAddress = reuse;
}

bool UdpSocket::reuseAddress() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_reuseAddress;
}

bool UdpSocket::bind(const HostAddress& hostAddress, std::uint16_t port)
{
    if (Executor* owner = ownerExecutor(); owner && !owner->isCurrent()) {
        return invokeOnOwnerThreadAndWait(
            owner,
            [self = NodePtr<UdpSocket>(this), hostAddress, port]() {
                return self ? self->bind(hostAddress, port) : false;
            },
            false);
    }

    transitionToUnbound();

    std::vector<sockaddr_storage> candidates;
    std::string resolveError;
    if (!hostAddress.resolve(port, HostResolveMode::Bind, candidates, resolveError)) {
        failWithErrno("Failed to resolve bind address", EHOSTUNREACH);
        return false;
    }

    int lastError = EHOSTUNREACH;

    for (const sockaddr_storage& candidate : candidates) {
        const sockaddr* address = reinterpret_cast<const sockaddr*>(&candidate);
        const int fd = ::socket(address->sa_family, SOCK_DGRAM, 0);
        if (fd < 0) {
            lastError = errno;
            continue;
        }

        if (setBlockingMode(fd, isBlocking()) < 0) {
            lastError = errno;
            ::close(fd);
            continue;
        }

        // Only on request: with SO_REUSEADDR, Linux lets two UDP sockets bind the same address and port.
        if (reuseAddress()) {
            int reuse = 1;
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
                lastError = errno;
                ::close(fd);
                continue;
            }
        }

        setDescriptor(fd);

        const socklen_t addrLen = socketAddressLength(candidate);
        const int result = ::bind(fd, address, addrLen);
        if (result == 0) {
            // Retrieve the actual bound port (useful if port 0 was passed)
            sockaddr_storage boundAddr;
            socklen_t boundAddrLen = sizeof(boundAddr);
            if (::getsockname(fd, reinterpret_cast<sockaddr*>(&boundAddr), &boundAddrLen) == 0) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_boundPort = addressToPort(boundAddr);
                }
            }

            applyBoundState();
            return true;
        }

        lastError = errno;
        setDescriptor(-1);
    }

    failWithErrno("bind() failed", lastError);
    return false;
}

std::uint16_t UdpSocket::boundPort() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_boundPort;
}

std::size_t UdpSocket::sendDatagram(const std::vector<std::uint8_t>& data,
                                     const HostAddress& destAddress,
                                     std::uint16_t destPort)
{
    return sendDatagram(Span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
                        destAddress, destPort);
}

std::size_t UdpSocket::sendDatagram(Span<const std::byte> data,
                                     const HostAddress& destAddress,
                                     std::uint16_t destPort)
{
    if (data.empty()) {
        return 0;
    }

    if (Executor* owner = ownerExecutor(); owner && !owner->isCurrent()) {
        const bool canQueue = [this]() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_state == UdpSocketState::Bound;
        }();

        if (!canQueue) {
            return 0;
        }

        const ByteArray::Storage pending(data.begin(), data.end());
        owner->post([self = NodePtr<UdpSocket>(this), pending, destAddress, destPort]() {
            if (self) {
                self->sendDatagram(Span<const std::byte>(pending.data(), pending.size()), destAddress, destPort);
            }
        });
        return data.size();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != UdpSocketState::Bound) {
            return 0;
        }
    }

    std::vector<sockaddr_storage> candidates;
    std::string resolveError;
    if (!destAddress.resolve(destPort, HostResolveMode::Connect, candidates, resolveError)) {
        failWithErrno("Failed to resolve destination address", EHOSTUNREACH);
        return 0;
    }

    for (const sockaddr_storage& candidate : candidates) {
        if (sendDatagramToAddress(data, candidate)) {
            return data.size();
        }
    }

    failWithErrno("sendto() failed on all candidates", errno);
    return 0;
}

std::size_t UdpSocket::sendDatagram(const std::string& data, const HostAddress& destAddress, std::uint16_t destPort)
{
    return sendDatagram(Span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
                        destAddress, destPort);
}

bool UdpSocket::hasPendingDatagram() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_pendingDatagrams.empty();
}

NetworkDatagram UdpSocket::pendingDatagram()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pendingDatagrams.empty()) {
        return NetworkDatagram();
    }

    NetworkDatagram result = m_pendingDatagrams.front();
    m_pendingDatagrams.pop();
    return result;
}

bool UdpSocket::joinMulticastGroup(const HostAddress& groupAddress, const HostAddress& interfaceAddress)
{
    if (Executor* owner = ownerExecutor(); owner && !owner->isCurrent()) {
        return invokeOnOwnerThreadAndWait(
            owner,
            [self = NodePtr<UdpSocket>(this), groupAddress, interfaceAddress]() {
                return self ? self->joinMulticastGroup(groupAddress, interfaceAddress) : false;
            },
            false);
    }

    const int fd = descriptor();
    if (fd < 0) {
        return false;
    }

    std::vector<sockaddr_storage> groupCandidates;
    std::vector<sockaddr_storage> interfaceCandidates;
    std::string unused;

    if (!groupAddress.resolve(0, HostResolveMode::Connect, groupCandidates, unused)) {
        return false;
    }

    if (!interfaceAddress.resolve(0, HostResolveMode::Bind, interfaceCandidates, unused)) {
        return false;
    }

    if (groupCandidates.empty() || interfaceCandidates.empty()) {
        return false;
    }

    const sockaddr_storage& group = groupCandidates[0];
    const sockaddr_storage& iface = interfaceCandidates[0];

    if (group.ss_family == AF_INET && iface.ss_family == AF_INET) {
        struct ip_mreq mreq;
        const sockaddr_in* group_in = reinterpret_cast<const sockaddr_in*>(&group);
        const sockaddr_in* iface_in = reinterpret_cast<const sockaddr_in*>(&iface);

        mreq.imr_multiaddr = group_in->sin_addr;
        mreq.imr_interface = iface_in->sin_addr;

        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            return false;
        }
        return true;
    }

    if (group.ss_family == AF_INET6 && iface.ss_family == AF_INET6) {
        struct ipv6_mreq mreq6;
        const sockaddr_in6* group_in6 = reinterpret_cast<const sockaddr_in6*>(&group);
        const sockaddr_in6* iface_in6 = reinterpret_cast<const sockaddr_in6*>(&iface);

        mreq6.ipv6mr_multiaddr = group_in6->sin6_addr;
        mreq6.ipv6mr_interface = iface_in6->sin6_scope_id;

        if (::setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            return false;
        }
        return true;
    }

    return false;
}

bool UdpSocket::leaveMulticastGroup(const HostAddress& groupAddress, const HostAddress& interfaceAddress)
{
    if (Executor* owner = ownerExecutor(); owner && !owner->isCurrent()) {
        return invokeOnOwnerThreadAndWait(
            owner,
            [self = NodePtr<UdpSocket>(this), groupAddress, interfaceAddress]() {
                return self ? self->leaveMulticastGroup(groupAddress, interfaceAddress) : false;
            },
            false);
    }

    const int fd = descriptor();
    if (fd < 0) {
        return false;
    }

    std::vector<sockaddr_storage> groupCandidates;
    std::vector<sockaddr_storage> interfaceCandidates;
    std::string unused;

    if (!groupAddress.resolve(0, HostResolveMode::Connect, groupCandidates, unused)) {
        return false;
    }

    if (!interfaceAddress.resolve(0, HostResolveMode::Bind, interfaceCandidates, unused)) {
        return false;
    }

    if (groupCandidates.empty() || interfaceCandidates.empty()) {
        return false;
    }

    const sockaddr_storage& group = groupCandidates[0];
    const sockaddr_storage& iface = interfaceCandidates[0];

    if (group.ss_family == AF_INET && iface.ss_family == AF_INET) {
        struct ip_mreq mreq;
        const sockaddr_in* group_in = reinterpret_cast<const sockaddr_in*>(&group);
        const sockaddr_in* iface_in = reinterpret_cast<const sockaddr_in*>(&iface);

        mreq.imr_multiaddr = group_in->sin_addr;
        mreq.imr_interface = iface_in->sin_addr;

        if (::setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            return false;
        }
        return true;
    }

    if (group.ss_family == AF_INET6 && iface.ss_family == AF_INET6) {
        struct ipv6_mreq mreq6;
        const sockaddr_in6* group_in6 = reinterpret_cast<const sockaddr_in6*>(&group);
        const sockaddr_in6* iface_in6 = reinterpret_cast<const sockaddr_in6*>(&iface);

        mreq6.ipv6mr_multiaddr = group_in6->sin6_addr;
        mreq6.ipv6mr_interface = iface_in6->sin6_scope_id;

        if (::setsockopt(fd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            return false;
        }
        return true;
    }

    return false;
}

UdpSocketState UdpSocket::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void UdpSocket::handleEvents(std::uint32_t nativeEvents)
{
    IOEvent::handleEvents(nativeEvents);

    if ((nativeEvents & (EPOLLERR | EPOLLHUP)) != 0u) {
        failWithErrno("Socket error condition", ECONNABORTED);
        transitionToError();
        return;
    }

    if ((nativeEvents & EPOLLIN) != 0u && state() == UdpSocketState::Bound) {
        handleReadable();
    }

    if ((nativeEvents & EPOLLOUT) != 0u && state() == UdpSocketState::Bound) {
        handleWritable();
    }
}

bool UdpSocket::sendDatagramToAddress(Span<const std::byte> data, const sockaddr_storage& destAddress)
{
    const int fd = descriptor();
    if (fd < 0) {
        return false;
    }

    const socklen_t addrLen = socketAddressLength(destAddress);
    const sockaddr* addr = reinterpret_cast<const sockaddr*>(&destAddress);

    const ssize_t sent = ::sendto(fd, data.data(), data.size(), MSG_NOSIGNAL, addr, addrLen);

    if (sent > 0) {
        emitBytesWritten(static_cast<std::size_t>(sent));
        return true;
    }

    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return false;
    }

    return false;
}

void UdpSocket::handleReadable()
{
    const int fd = descriptor();
    if (fd < 0) {
        return;
    }

    bool appended = false;
    std::uint8_t buffer[4096];
    sockaddr_storage senderAddr;
    socklen_t senderAddrLen = sizeof(senderAddr);

    while (true) {
        std::memset(&senderAddr, 0, sizeof(senderAddr));
        senderAddrLen = sizeof(senderAddr);

        const ssize_t readBytes = ::recvfrom(fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&senderAddr), &senderAddrLen);

        if (readBytes > 0) {
            std::string senderHost = addressToString(senderAddr);
            std::uint16_t senderPort = addressToPort(senderAddr);

            NetworkDatagram datagram(ByteArray(buffer, static_cast<std::size_t>(readBytes)), senderHost, senderPort);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pendingDatagrams.push(datagram);
            }
            appended = true;
            continue;
        }

        if (readBytes == 0) {
            break;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        failWithErrno("recvfrom() failed", errno);
        break;
    }

    if (appended) {
        emitReadyRead();
    }
}

void UdpSocket::handleWritable()
{
    // UDP is typically ready to write immediately after bind().
    // This handler is a placeholder for future optimizations.
    updateInterestForState();
}

void UdpSocket::applyBoundState()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UdpSocketState::Bound;
    }

    updateInterestForState();
    start();
}

void UdpSocket::failWithErrno(const std::string& prefix, int errorCode)
{
    transitionToError();
    emitErrorOccurred(prefix + ": " + std::strerror(errorCode));
}

void UdpSocket::updateInterestForState()
{
    IOEventFlags flags = IOEventFlags::Error;

    if (state() == UdpSocketState::Bound) {
        flags |= IOEventFlags::Read;
    }

    setInterest(flags);
}

void UdpSocket::emitReadyRead()
{
    if (Executor* owner = ownerExecutor()) {
        owner->post([self = NodePtr<UdpSocket>(this)]() {
            if (self) {
                self->readyRead.emit();
            }
        });
        return;
    }

    readyRead.emit();
}

void UdpSocket::emitBytesWritten(std::size_t written)
{
    if (Executor* owner = ownerExecutor()) {
        owner->post([self = NodePtr<UdpSocket>(this), written]() {
            if (self) {
                self->bytesWritten.emit(written);
            }
        });
        return;
    }

    bytesWritten.emit(written);
}

void UdpSocket::emitErrorOccurred(std::string message)
{
    if (Executor* owner = ownerExecutor()) {
        owner->post([self = NodePtr<UdpSocket>(this), message = std::move(message)]() {
            if (self) {
                self->errorOccurred.emit(message);
            }
        });
        return;
    }

    errorOccurred.emit(message);
}

void UdpSocket::transitionToError()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state = UdpSocketState::Error;
}

void UdpSocket::transitionToUnbound()
{
    stop();
    setDescriptor(-1);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = UdpSocketState::Unbound;
        m_boundPort = 0;
        std::queue<NetworkDatagram> empty;
        m_pendingDatagrams.swap(empty);
    }
}

}  // namespace snf
