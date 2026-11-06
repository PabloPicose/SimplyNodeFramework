#pragma once

/**
 * @file IoService.h
 * @brief Descriptor-watching thread for IOEvents owned by strands (internal).
 * @ingroup SNFCore
 */

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace snf {

class EpollPoller;

namespace detail {

class Strand;

/**
 * @class IoService
 * @ingroup SNFCore
 * @brief Waits for readiness of strand-owned descriptors and posts each
 *        callback to the descriptor's strand.
 *
 * One per TaskPool; its thread and epoll instance start with the first
 * registered descriptor. A descriptor is armed with EPOLLONESHOT: once it
 * reports, epoll keeps it quiet until its callback has run in the strand and
 * re-armed it, so a busy strand neither gets duplicate callbacks nor makes
 * this thread spin on a still-ready descriptor.
 */
class IoService
{
public:
    using Callback = std::function<void(std::uint32_t)>;

    IoService();
    ~IoService();

    IoService(const IoService&) = delete;
    IoService& operator=(const IoService&) = delete;

    /** @brief Watches @p fd for @p events for @p strand (replacing a previous watch). Ignored after stop(). */
    void registerIO(Strand* strand, int fd, std::uint32_t events, Callback callback);

    /** @brief Changes the events of a watched @p fd. */
    void modifyIO(int fd, std::uint32_t events);

    /** @brief Stops watching @p fd. Safe at any time, also after stop(). */
    void unregisterIO(int fd);

    /** @brief Returns `true` if @p fd is watched. */
    bool hasIOWatch(int fd) const;

    /** @brief Stops and joins the thread; no callback is posted afterwards. Idempotent. */
    void stop();

    /** @brief Returns the number of watched descriptors. */
    std::size_t watchCount() const;

private:
    struct Watch {
        Strand* strand = nullptr;
        std::uint32_t events = 0;
        Callback callback;
        bool queued = false;            // a callback is posted and has not finished
        std::uint64_t registration = 0;  // tells re-registrations of a reused fd apart
    };

    void run();
    // Inside the strand, after the callback: clears `queued` and re-arms.
    void markHandled(int fd, std::uint64_t registration);
    void arm(int fd, std::uint32_t events);

    mutable std::mutex m_mutex;
    std::unique_ptr<EpollPoller> m_poller;
    std::unordered_map<int, Watch> m_watches;
    std::thread m_thread;
    std::uint64_t m_nextRegistration = 1;
    bool m_stopped = false;
};

}  // namespace detail
}  // namespace snf
