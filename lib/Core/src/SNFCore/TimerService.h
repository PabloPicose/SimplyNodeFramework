#pragma once

/**
 * @file TimerService.h
 * @brief Deadline thread for Timers owned by strands (internal).
 * @ingroup SNFCore
 */

#include <SNFCore/NodePtr.h>
#include <SNFCore/Timer.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace snf::detail {

class Strand;

/**
 * @class TimerService
 * @ingroup SNFCore
 * @brief Waits for the deadlines of strand-owned Timers and posts each
 *        timeout to the timer's strand.
 *
 * One per TaskPool; its thread starts with the first scheduled timer. It only
 * keeps time: the timeout itself (Timer::dispatchTimeout) runs inside the
 * strand, and is dropped there if the timer no longer belongs to it (the
 * timer migrated after the timeout was posted; its new owner has it armed).
 */
class TimerService
{
public:
    using Clock = std::chrono::steady_clock;

    TimerService() = default;
    ~TimerService();

    TimerService(const TimerService&) = delete;
    TimerService& operator=(const TimerService&) = delete;

    /**
     * @brief Arms @p timer (replacing a previous deadline) for @p strand.
     *
     * Called inside @p strand, while @p timer is alive. Ignored after stop().
     */
    void schedule(Strand* strand, Timer* timer, Clock::time_point deadline, std::uint64_t generation);

    /** @brief Disarms @p timer if armed. Safe at any time, also after stop(). */
    void cancel(Timer* timer);

    /** @brief Stops and joins the thread; pending timeouts never fire. Idempotent. */
    void stop();

    /** @brief Returns the number of armed timers. */
    std::size_t armedCount() const;

private:
    struct Entry {
        Strand* strand;
        NodePtr<Timer> timer;  // taken while alive: never rebuilt from a raw pointer
        std::uint64_t generation;
    };
    using Queue = std::multimap<Clock::time_point, Entry>;

    void run();

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    Queue m_queue;
    std::unordered_map<const Timer*, Queue::iterator> m_index;
    std::thread m_thread;
    bool m_stopped = false;
};

}  // namespace snf::detail
