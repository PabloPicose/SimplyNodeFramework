#include "SNFCore/IoService.h"

#include "SNFCore/EpollPoller.h"
#include "SNFCore/Strand.h"

#include <utility>
#include <vector>

#ifndef SNF_PLATFORM_WEB
#include <sys/epoll.h>
#endif

namespace snf::detail {

namespace {

#ifndef SNF_PLATFORM_WEB
constexpr std::uint32_t kOneShot = EPOLLONESHOT;
#else
constexpr std::uint32_t kOneShot = 0;
#endif

}  // namespace

IoService::IoService() = default;

IoService::~IoService() { stop(); }

void IoService::arm(int fd, std::uint32_t events)
{
    // Caller holds m_mutex. A descriptor closed under us makes epoll_ctl fail:
    // its owner unregisters it right after, so a failure here is not fatal.
    try {
        m_poller->addOrUpdate(fd, events | kOneShot);
    } catch (const std::exception&) {
    }
}

void IoService::registerIO(Strand* strand, int fd, std::uint32_t events, Callback callback)
{
    if (fd < 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopped) {
        return;
    }
    if (! m_poller) {
        m_poller = std::make_unique<EpollPoller>();
    }
    Watch& watch = m_watches[fd];
    watch.strand = strand;
    watch.events = events;
    watch.callback = std::move(callback);
    watch.queued = false;
    watch.registration = m_nextRegistration++;
    arm(fd, events);
#ifndef SNF_PLATFORM_WEB
    if (! m_thread.joinable()) {
        m_thread = std::thread([this]() { run(); });
    }
#endif
}

void IoService::modifyIO(int fd, std::uint32_t events)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_watches.find(fd);
    if (found == m_watches.end()) {
        return;
    }
    found->second.events = events;
    if (! found->second.queued && ! m_stopped) {
        arm(fd, events);  // while queued, markHandled() re-arms with the new events
    }
}

void IoService::unregisterIO(int fd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_watches.erase(fd) > 0 && m_poller) {
        try {
            m_poller->remove(fd);
        } catch (const std::exception&) {
        }
    }
}

bool IoService::hasIOWatch(int fd) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_watches.count(fd) > 0;
}

std::size_t IoService::watchCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_watches.size();
}

void IoService::stop()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        thread.swap(m_thread);
        if (m_poller) {
            m_poller->wakeUp();
        }
    }
    if (thread.joinable()) {
        thread.join();
    }
}

void IoService::markHandled(int fd, std::uint64_t registration)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_watches.find(fd);
    if (found == m_watches.end() || found->second.registration != registration) {
        return;  // unregistered (or re-registered) by the callback itself
    }
    found->second.queued = false;
    if (! m_stopped) {
        arm(fd, found->second.events);
    }
}

void IoService::run()
{
    for (;;) {
        // m_poller is created before the thread and never replaced: safe to
        // wait on it without the lock.
        const std::vector<EpollPoller::Event> ready = m_poller->wait(-1);

        struct Ready {
            Strand* strand;
            int fd;
            std::uint32_t events;
            std::uint64_t registration;
            Callback callback;
        };
        std::vector<Ready> toPost;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) {
                return;
            }
            for (const EpollPoller::Event& event : ready) {
                auto found = m_watches.find(event.fd);
                if (found == m_watches.end() || found->second.queued) {
                    continue;
                }
                found->second.queued = true;
                toPost.push_back(
                    Ready{found->second.strand, event.fd, event.events, found->second.registration, found->second.callback});
            }
        }

        for (Ready& item : toPost) {
            item.strand->post([this, fd = item.fd, events = item.events, registration = item.registration,
                               callback = std::move(item.callback)]() {
                callback(events);
                markHandled(fd, registration);
            });
        }
    }
}

}  // namespace snf::detail
