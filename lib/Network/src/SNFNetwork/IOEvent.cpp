#include "SNFNetwork/IOEvent.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/Executor.h"
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
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([self = NodePtr<IOEvent>(this), fd]() {
            if (self) {
                self->setDescriptor(fd);
            }
        });
        return;
    }

    int previousFd = -1;
    int currentFd = -1;
    bool wasActive = false;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_fd == fd) {
            return;
        }

        previousFd = m_fd;
        wasActive = m_active;
        m_fd = fd;
        ++m_registrationGeneration;
        generation = m_registrationGeneration;
        currentFd = m_fd;
    }

    if (Executor* owner = ownerExecutor(); owner) {
        if (wasActive && previousFd >= 0) {
            owner->unregisterIO(previousFd);
        }

        if (wasActive && currentFd >= 0) {
            owner->registerIO(currentFd,
                             interestToNative(interest()),
                             makeDispatchCallback(currentFd, generation));
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
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([self = NodePtr<IOEvent>(this), flags]() {
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
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([self = NodePtr<IOEvent>(this)]() {
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
            ++m_registrationGeneration;
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
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([self = NodePtr<IOEvent>(this)]() {
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
        if (m_active) {
            m_active = false;
            ++m_registrationGeneration;
        }
    }

    if (wasActive && fd >= 0) {
        if (Executor* owner = ownerExecutor()) {
            owner->unregisterIO(fd);
        }
    }
}

bool IOEvent::isActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active;
}

void IOEvent::update() {}

void IOEvent::onAboutToMoveToThread(EventLoop* /*newLoop*/)
{
    // Unregister the fd from the current (old) owner so it stops delivering
    // callbacks there before the affinity changes.
    int fd = -1;
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fd = m_fd;
        wasActive = m_active;
    }

    if (wasActive && fd >= 0) {
        if (Executor* owner = ownerExecutor()) {
            owner->unregisterIO(fd);
        }
    }
}

void IOEvent::onMovedToThread(EventLoop* /*oldLoop*/)
{
    // Re-register the fd with the new owner (ownerExecutor() already is the
    // new one: Node::applyMoveTo() updates the owner before this hook).
    int fd = -1;
    bool active = false;
    IOEventFlags interestFlags = IOEventFlags::None;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fd = m_fd;
        active = m_active;
        interestFlags = m_interest;
        ++m_registrationGeneration;
        generation = m_registrationGeneration;
    }

    if (active && fd >= 0) {
        if (Executor* owner = ownerExecutor()) {
            owner->registerIO(fd, interestToNative(interestFlags), makeDispatchCallback(fd, generation));
        }
    }
}

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
    if (Executor* owner = ownerExecutor()) {
        int fd = -1;
        IOEventFlags interestFlags = IOEventFlags::None;
        bool active = false;
        std::uint64_t generation = 0;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            fd = m_fd;
            interestFlags = m_interest;
            active = m_active;
            generation = m_registrationGeneration;
        }

        if (fd < 0) {
            return;
        }

        if (wasActive && owner->hasIOWatch(fd)) {
            owner->modifyIO(fd, interestToNative(interestFlags));
            return;
        }

        if (active) {
            owner->registerIO(fd, interestToNative(interestFlags), makeDispatchCallback(fd, generation));
        }
    }
}

bool IOEvent::isRegistrationCurrent(int fd, std::uint64_t generation) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fd == fd && m_registrationGeneration == generation && m_active;
}

std::function<void(std::uint32_t)> IOEvent::makeDispatchCallback(int fd, std::uint64_t generation)
{
    return [self = NodePtr<IOEvent>(this), fd, generation](std::uint32_t events) {
        // The callback runs inside the executor that registered the fd. If the
        // node has moved to another owner since (its readiness was already
        // queued here), drop it without touching the node: only its new owner
        // may, and it has the fd registered again. Inside the owner the node
        // cannot be destroyed under us, so the checks below are stable.
        const Executor* owner = self.ownerExecutor();
        if (! owner || ! owner->isCurrent() || ! self) {
            return;
        }
        if (self->isRegistrationCurrent(fd, generation)) {
            self->handleEvents(events);
        }
    };
}

}  // namespace snf
