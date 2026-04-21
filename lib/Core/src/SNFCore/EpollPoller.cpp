#include "SNFCore/EpollPoller.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace snf {

namespace {

constexpr int kMaxEvents = 32;

}  // namespace

EpollPoller::EpollPoller()
{
    m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        throw std::runtime_error(std::string("epoll_create1 failed: ") + std::strerror(errno));
    }

    m_wakeupFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeupFd < 0) {
        ::close(m_epollFd);
        m_epollFd = -1;
        throw std::runtime_error(std::string("eventfd failed: ") + std::strerror(errno));
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = m_wakeupFd;
    if (::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_wakeupFd, &event) < 0) {
        const std::string error = std::strerror(errno);
        ::close(m_wakeupFd);
        ::close(m_epollFd);
        m_wakeupFd = -1;
        m_epollFd = -1;
        throw std::runtime_error(std::string("epoll_ctl add wakeup fd failed: ") + error);
    }
}

EpollPoller::~EpollPoller()
{
    if (m_wakeupFd >= 0) {
        ::close(m_wakeupFd);
        m_wakeupFd = -1;
    }

    if (m_epollFd >= 0) {
        ::close(m_epollFd);
        m_epollFd = -1;
    }
}

void EpollPoller::addOrUpdate(int fd, std::uint32_t events)
{
    if (fd < 0) {
        return;
    }

    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (::epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &event) == 0) {
        return;
    }

    if (errno != ENOENT) {
        throw std::runtime_error(std::string("epoll_ctl mod failed: ") + std::strerror(errno));
    }

    if (::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &event) < 0) {
        throw std::runtime_error(std::string("epoll_ctl add failed: ") + std::strerror(errno));
    }
}

void EpollPoller::remove(int fd)
{
    if (fd < 0) {
        return;
    }

    if (::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr) == 0) {
        return;
    }

    if (errno == ENOENT || errno == EBADF) {
        return;
    }

    throw std::runtime_error(std::string("epoll_ctl del failed: ") + std::strerror(errno));
}

void EpollPoller::wakeUp()
{
    if (m_wakeupFd < 0) {
        return;
    }

    std::uint64_t value = 1;
    while (::write(m_wakeupFd, &value, sizeof(value)) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN) {
            return;
        }
        return;
    }
}

std::vector<EpollPoller::Event> EpollPoller::wait(int timeoutMs)
{
    if (m_epollFd < 0) {
        return {};
    }

    epoll_event nativeEvents[kMaxEvents]{};
    int readyCount = 0;

    do {
        readyCount = ::epoll_wait(m_epollFd, nativeEvents, kMaxEvents, timeoutMs);
    } while (readyCount < 0 && errno == EINTR);

    if (readyCount <= 0) {
        return {};
    }

    std::vector<Event> events;
    events.reserve(static_cast<std::size_t>(readyCount));

    for (int i = 0; i < readyCount; ++i) {
        const int fd = nativeEvents[i].data.fd;
        if (fd == m_wakeupFd) {
            drainWakeupFd();
            continue;
        }

        events.push_back(Event{fd, nativeEvents[i].events});
    }

    return events;
}

void EpollPoller::drainWakeupFd()
{
    if (m_wakeupFd < 0) {
        return;
    }

    std::uint64_t value = 0;
    while (::read(m_wakeupFd, &value, sizeof(value)) >= 0) {
    }

    if (errno == EINTR) {
        drainWakeupFd();
    }
}

}  // namespace snf
