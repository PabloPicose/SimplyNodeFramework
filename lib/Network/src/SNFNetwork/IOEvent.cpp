#include "SNFNetwork/IOEvent.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"

#include <cerrno>

#include <sys/epoll.h>
#include <unistd.h>

namespace snf {

IOEvent::IOEvent(Node* parent) : Node(parent) {}

IOEvent::~IOEvent()
{
    stop();

    const int fd = descriptor();
    if (fd >= 0) {
        ::close(fd);
    }
}

void IOEvent::setDescriptor(int fd)
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<IOEvent>(this), fd]() {
            if (self) {
                self->setDescriptor(fd);
            }
        });
        return;
    }

    int previousFd = -1;
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_fd == fd) {
            return;
        }

        previousFd = m_fd;
        wasActive = m_active;
        m_fd = fd;
    }

    if (EventLoop* loop = ownerEventLoop(); loop) {
        if (wasActive && previousFd >= 0) {
            loop->unregisterIO(previousFd);
        }
        if (wasActive && m_fd >= 0) {
            loop->registerIO(m_fd,
                             interestToNative(interest()),
                             [self = NodePtr<IOEvent>(this)](std::uint32_t events) {
                                 if (self) {
                                     self->handleEvents(events);
                                 }
                             });
        }
    }

    if (previousFd >= 0) {
        ::close(previousFd);
    }
}

int IOEvent::descriptor() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fd;
}

void IOEvent::setInterest(IOEventFlags flags)
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<IOEvent>(this), flags]() {
            if (self) {
                self->setInterest(flags);
            }
        });
        return;
    }

    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_interest == flags) {
            return;
        }

        m_interest = flags;
        wasActive = m_active;
    }

    syncRegistration(wasActive);
}

IOEventFlags IOEvent::interest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_interest;
}

void IOEvent::start()
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<IOEvent>(this)]() {
            if (self) {
                self->start();
            }
        });
        return;
    }

    bool shouldStart = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (! m_active) {
            m_active = true;
            shouldStart = true;
        }
    }

    syncRegistration(shouldStart);
}

void IOEvent::start(IOEventFlags flags)
{
    setInterest(flags);
    start();
}

void IOEvent::stop()
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<IOEvent>(this)]() {
            if (self) {
                self->stop();
            }
        });
        return;
    }

    int fd = -1;
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fd = m_fd;
        wasActive = m_active;
        m_active = false;
    }

    if (wasActive && fd >= 0) {
        if (EventLoop* loop = ownerEventLoop()) {
            loop->unregisterIO(fd);
        }
    }
}

bool IOEvent::isActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active;
}

void IOEvent::update() {}

void IOEvent::handleEvents(std::uint32_t nativeEvents)
{
    const IOEventFlags flags = nativeToInterest(nativeEvents);

    if (hasAny(flags, IOEventFlags::Error)) {
        error.emit(errno);
    }
    if (hasAny(flags, IOEventFlags::HangUp)) {
        hangUp.emit();
    }
    if (hasAny(flags, IOEventFlags::Read)) {
        readable.emit();
    }
    if (hasAny(flags, IOEventFlags::Write)) {
        writable.emit();
    }
}

std::uint32_t IOEvent::interestToNative(IOEventFlags flags) const
{
    std::uint32_t native = EPOLLERR | EPOLLHUP;
    if (hasAny(flags, IOEventFlags::Read)) {
        native |= EPOLLIN;
    }
    if (hasAny(flags, IOEventFlags::Write)) {
        native |= EPOLLOUT;
    }
    return native;
}

IOEventFlags IOEvent::nativeToInterest(std::uint32_t nativeEvents) const
{
    IOEventFlags flags = IOEventFlags::None;
    if ((nativeEvents & EPOLLIN) != 0u) {
        flags |= IOEventFlags::Read;
    }
    if ((nativeEvents & EPOLLOUT) != 0u) {
        flags |= IOEventFlags::Write;
    }
    if ((nativeEvents & EPOLLERR) != 0u) {
        flags |= IOEventFlags::Error;
    }
    if ((nativeEvents & EPOLLHUP) != 0u || (nativeEvents & EPOLLRDHUP) != 0u) {
        flags |= IOEventFlags::HangUp;
    }
    return flags;
}

void IOEvent::syncRegistration(bool wasActive)
{
    if (EventLoop* loop = ownerEventLoop()) {
        const int fd = descriptor();
        if (fd < 0) {
            return;
        }

        if (wasActive && loop->hasIOWatch(fd)) {
            loop->modifyIO(fd, interestToNative(interest()));
            return;
        }

        if (isActive()) {
            loop->registerIO(fd,
                             interestToNative(interest()),
                             [self = NodePtr<IOEvent>(this)](std::uint32_t events) {
                                 if (self) {
                                     self->handleEvents(events);
                                 }
                             });
        }
    }
}

}  // namespace snf
