#pragma once

/**
 * @file Timer.h
 * @brief Repeating and single-shot timers driven by the EventLoop.
 * @ingroup SNFCore_Timers
 */

#include <SNFCore/Connection.h>
#include <SNFCore/Node.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

namespace snf {

class EventLoop;

/**
 * @class Timer
 * @ingroup SNFCore_Timers
 * @brief Repeating or single-shot timer that fires on its owner thread.
 *
 * `Timer` schedules the `timeout` signal to be emitted at a fixed interval
 * via the owner thread's `EventLoop`. Internally it interacts with
 * `EventLoop::scheduleTimer()` and is therefore safe only on the thread
 * that created it.
 *
 * **Repeating timer:**
 * @code
 * snf::Timer t;
 * t.setInterval(std::chrono::milliseconds(500));
 * t.timeout.connect([]() { // called every 500 ms }});
 * t.start();
 * @endcode
 *
 * **One-shot (free-standing helper):**
 * @code
 * snf::Timer::singleShot(std::chrono::milliseconds(1000),
 *                        []() { // called once after 1 s });
 * @endcode
 *
 * @sa Signal, EventLoop, Application
 */
class Timer final : public Node
{
public:
    /** @brief Alias for the duration type used by Timer (milliseconds). */
    using Duration = std::chrono::milliseconds;

    /** @brief Constructs a Timer, optionally attaching it to @p parent. */
    explicit Timer(Node* parent = nullptr);
    ~Timer() override;

    /** @brief Starts the timer using the previously configured interval. */
    void start();

    /**
     * @brief Starts the timer with the given @p interval, overwriting any
     *        previously set value.
     */
    void start(Duration interval);

    /** @brief Overload accepting an integer number of milliseconds. */
    void start(int intervalMs);

    /**
     * @brief Stops the timer.
     *
     * If the timer is already inactive this is a no-op. Safe to call from
     * within the `timeout` signal handler.
     */
    void stop();

    /** @brief Sets the interval without restarting the timer. */
    void setInterval(Duration interval);

    /** @brief Overload accepting an integer number of milliseconds. */
    void setInterval(int intervalMs);

    /** @brief Returns the currently configured interval. */
    Duration interval() const;

    /**
     * @brief Configures single-shot mode.
     *
     * When `true`, the timer emits `timeout` exactly once and then stops
     * automatically.
     */
    void setSingleShot(bool singleShot);

    /** @brief Returns `true` if the timer is in single-shot mode. */
    bool isSingleShot() const;

    /** @brief Returns `true` if the timer has been started and not yet stopped. */
    bool isActive() const;

    /**
     * @brief Creates a self-deleting single-shot timer that calls @p func
     *        after @p interval.
     *
     * The returned `Timer*` is owned by its parent (if given) or by the
     * Application root list. It deletes itself after firing.
     *
     * @param interval Time to wait before firing.
     * @param func     Callable invoked on the timer's owner thread.
     * @param parent   Optional parent node; if null the timer is a root node.
     * @return Pointer to the timer (already started; do not `delete` manually).
     */
    template <typename Func>
    static Timer* singleShot(Duration interval, Func&& func, Node* parent = nullptr)
    {
        auto* timer = new Timer(parent);
        timer->setSingleShot(true);
        timer->timeout.connect(std::function<void()>(std::forward<Func>(func)));
        timer->timeout.connect(NodePtr<Timer>(timer), [](Timer& self) { self.deleteLater(); });
        timer->start(interval);
        return timer;
    }

    /**
     * @brief Creates a self-deleting single-shot timer that invokes a member
     *        function of @p receiver after @p interval.
     *
     * The connection respects @p receiver's lifetime via `NodePtr`: if the
     * receiver is deleted before the timer fires, the slot is not called.
     *
     * @param interval Time to wait before firing.
     * @param receiver Safe reference to the target node.
     * @param method   Member function to call on @p receiver.
     * @param parent   Optional parent node.
     * @param type     Connection type (default: `Queued` for cross-thread safety).
     */
    template <typename Receiver>
    static Timer* singleShot(Duration interval,
                             NodePtr<Receiver> receiver,
                             void (Receiver::*method)(),
                             Node* parent = nullptr,
                             ConnectionType type = ConnectionType::Queued)
    {
        auto* timer = new Timer(parent);
        timer->setSingleShot(true);
        timer->timeout.connect(receiver, method, type);
        timer->timeout.connect(NodePtr<Timer>(timer), [](Timer& self) { self.deleteLater(); });
        timer->start(interval);
        return timer;
    }

    /**
     * @brief Emitted when the timer interval elapses.
     *
     * Always fires on the timer's owner thread. For cross-thread handling
     * use a `Queued` connection.
     */
    Signal<> timeout;

protected:
    void update() override;

private:
    friend class EventLoop;

    void dispatchTimeout(std::uint64_t generation);

    mutable std::mutex m_mutex;
    Duration m_interval{Duration::zero()};
    bool m_singleShot = false;
    bool m_active = false;
    std::uint64_t m_generation = 0;
};

}  // namespace snf
