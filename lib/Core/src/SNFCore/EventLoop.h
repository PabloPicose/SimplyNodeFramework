#pragma once

/**
 * @file EventLoop.h
 * @brief Per-thread event dispatcher: tasks, timers, and asynchronous I/O.
 * @ingroup SNFCore_Application
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace snf {
class Node;
class TimeLapse;
class Timer;
class EpollPoller;

/**
 * @class EventLoop
 * @ingroup SNFCore_Application
 * @brief Drives the execution of tasks, timers, and I/O events on a single
 *        thread.
 *
 * Each thread that participates in the framework has at most one `EventLoop`.
 * The main thread's EventLoop is created by `Application`. Additional threads
 * obtain theirs via `Application::getOrCreateCurrentThreadEventLoop()`.
 *
 * The loop processes three kinds of work:
 * - **Tasks** — arbitrary `std::function<void()>` callables posted via `post()`.
 * - **Timers** — `Timer` objects scheduled via `scheduleTimer()`.
 * - **I/O** — file-descriptor readiness queued by the platform dispatcher.
 *
 * On native Linux builds, file descriptors are watched by a dedicated epoll
 * thread that sleeps in `epoll_wait()` until an fd becomes ready. The resulting
 * callbacks are queued back to this owner `EventLoop`, so socket code still
 * executes on the thread that owns the object. WebAssembly builds use the
 * browser/event-system specific path and do not start an epoll thread.
 *
 * @note `EventLoop` is not part of the public library API that end-users
 *       call directly. Prefer `Application::run()`, `Timer`, and node signals
 *       for all application-level scheduling.
 */
class EventLoop
{
public:
    /** @brief Callable type for tasks posted to the EventLoop. */
    using Task = std::function<void()>;

    EventLoop();
    ~EventLoop();

    /**
     * @brief Posts a task to be executed on this EventLoop's thread.
     *
     * Thread-safe: may be called from any thread. The task is delivered by the
     * owning event loop thread.
     */
    void post(Task t);

    /** @brief Enqueues @p node for deferred deletion on the next iteration. */
    void enqueueDelete(Node* node);

    /** @brief Registers @p node so it receives per-iteration `run()` calls. */
    void addNode(Node* node);

    /** @brief Unregisters @p node from per-iteration `run()` calls. */
    void removeNode(Node* node);

    /** @brief Adds @p node to the root-node list managed by this EventLoop. */
    void addRootNode(Node* node);

    /** @brief Removes @p node from the root-node list. */
    void removeRootNode(Node* node);

    /** @brief Returns the number of root nodes registered with this EventLoop. */
    std::size_t getRootNodesCount() const;

    /** @brief Returns the root node at @p index. */
    Node* getRootNode(std::size_t index) const;

    /** @brief Returns the number of root nodes pending deferred deletion. */
    std::size_t getRootNodesToDeleteCount() const;

    /**
     * @brief Starts the event loop and blocks until `stop()` is called.
     *
     * Processes tasks, due timers, queued I/O callbacks, and root nodes. When
     * idle, the loop sleeps on a condition variable until there is work to do.
     */
    void run();

    /**
     * @brief Performs a single non-blocking pass of the event loop.
     *
     * Drains the task queue, fires any due timers, delivers any queued I/O
     * callbacks, and ticks all root nodes — then returns
     * immediately without waiting for future work.
     *
     * This is the intended integration point for WebAssembly builds, where
     * the browser controls the event loop and supplies a per-frame callback
     * (see `SNFWidgets::WebApplicationNode`).  Calling `run()` in that
     * context would spin-poll because the Emscripten I/O stub returns
     * immediately from every `wait()` call.
     *
     * Safe to call from the owner thread only.
     */
    void runPendingWork();

    /**
     * @brief Requests the event loop to exit after the current iteration.
     *
     * Thread-safe. The loop will finish processing the current set of
     * ready events before returning from `run()`.
     */
    void stop();

    /**
     * @brief Schedules @p timer to fire at @p deadline.
     *
     * Called internally by `Timer::start()`. @p generation is used to
     * discard stale callbacks after a timer has been restarted.
     */
    void scheduleTimer(Timer* timer, std::chrono::steady_clock::time_point deadline, std::uint64_t generation);

    /** @brief Cancels a previously scheduled timer. Called by `Timer::stop()`. */
    void cancelTimer(Timer* timer);

    /**
     * @brief Registers an epoll interest set for file descriptor @p fd.
     * @param fd       The file descriptor to watch.
     * @param events   Epoll event flags (e.g. `EPOLLIN | EPOLLOUT`).
     * @param callback Invoked with the ready event mask when events arrive.
     */
    void registerIO(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback);

    /** @brief Updates the epoll interest set for an already-registered @p fd. */
    void modifyIO(int fd, std::uint32_t events);

    /** @brief Removes @p fd from epoll watch. */
    void unregisterIO(int fd);

    /** @brief Returns `true` if @p fd is currently watched by this EventLoop. */
    bool hasIOWatch(int fd) const;

    /** @brief Returns `true` if the calling thread is the owner thread of this EventLoop. */
    bool isInThisThread() const noexcept;

    /** @brief Returns the ID of the thread that owns this EventLoop. */
    std::thread::id ownerThreadId() const noexcept;

    /** @brief Returns the number of nodes pending deferred deletion. */
    std::size_t pendingDeleteCount() const;

    /** @brief Returns the total number of nodes registered with this EventLoop. */
    std::size_t registeredNodesCount() const;

    /** @brief Returns the number of active (scheduled) timers. */
    std::size_t activeTimerCount() const;

    /** @brief Returns the age in nanoseconds since this loop last did work. */
    std::uint64_t lastActivityAgeNanoseconds() const;

    /** @brief Returns the duration in nanoseconds of the last loop pass. */
    std::uint64_t lastIterationDurationNanoseconds() const;

    /**
     * @brief Returns `true` if there are no pending tasks, deletes, or
     *        due timers.
     */
    bool hasPendingWork() const;

private:
    bool hasPendingWorkLocked() const;
    bool popNextTask(Task& task);
    std::vector<Node*> takePendingDeletes();

    struct TimerEntry {
        Timer* timer = nullptr;
        std::chrono::steady_clock::time_point deadline{};
        std::uint64_t generation = 0;
    };

    bool nextTimerDeadlineLocked(std::chrono::steady_clock::time_point& deadline) const;
    std::vector<TimerEntry> takeDueTimers(std::chrono::steady_clock::time_point now);

    struct IOWatchEntry {
        std::uint32_t events = 0;
        std::function<void(std::uint32_t)> callback;
        bool queued = false;
    };

    struct ReadyIOEntry {
        int fd = -1;
        std::uint32_t events = 0;
        std::function<void(std::uint32_t)> callback;
    };

    std::vector<ReadyIOEntry> takeReadyIO();
    void markReadyIOHandled(int fd);
    void ensureIoThreadRunning();
    void stopIoThread();
    void ioThreadMain();

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::queue<Task> m_tasks;
    std::vector<Node*> m_nodesToDelete;
    std::vector<Node*> m_nodes;
    std::vector<Node*> m_rootNodes;
    std::vector<TimerEntry> m_timers;
    std::unordered_map<int, IOWatchEntry> m_ioWatches;
    std::vector<ReadyIOEntry> m_readyIO;
    std::unique_ptr<EpollPoller> m_ioPoller;
    std::thread m_ioThread;
    bool m_ioThreadStarted = false;
    std::atomic_bool m_ioThreadStop{false};
    bool m_stop = false;
    const std::thread::id m_owner;
    TimeLapse* m_timeLapse = nullptr;
};
}  // namespace snf
