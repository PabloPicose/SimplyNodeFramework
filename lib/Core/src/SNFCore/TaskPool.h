#pragma once

/**
 * @file TaskPool.h
 * @brief Worker pool with a shared task queue, for CPU work.
 * @ingroup SNFCore
 */

#include <SNFCore/Runnable.h>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace snf {

namespace detail {
class IoService;
class Strand;
class TimerService;
}

/**
 * @class TaskPool
 * @ingroup SNFCore
 * @brief Runs tasks on a bounded set of worker threads that share one queue.
 *
 * Every worker takes the next queued task as soon as it is free, so a long
 * task on one worker never holds back the tasks queued after it while another
 * worker is idle. Use it for CPU work (`Runnable` or plain callables).
 *
 * Unlike `ThreadPool`, the workers have **no EventLoop**: they cannot host
 * nodes. Constructing a `Node` on a worker throws `std::logic_error`. To
 * report results, emit a signal (it is delivered to receiver nodes on their
 * own thread, see `ConnectionType::Auto`) or post to a node's EventLoop.
 *
 * A task must not block waiting for another task of the same pool: with every
 * worker blocked that way, the pool deadlocks.
 *
 * The pool also runs `PooledNode`s (see `PooledNode::moveToTaskPool()`):
 * each pooled subtree gets a strand that runs its work serially on whichever
 * worker is free.
 *
 * @code
 * snf::TaskPool::globalInstance()->start([]() { crunchNumbers(); });
 * @endcode
 */
class TaskPool
{
public:
    using Task = std::function<void()>;

    /** @brief Starts @p maxThreadCount workers (at least one, none on web). */
    explicit TaskPool(std::size_t maxThreadCount = defaultThreadCount());

    /** @brief Calls shutdown(). */
    ~TaskPool();

    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    /** @brief Returns the Application-owned global TaskPool, if an Application exists. */
    static TaskPool* globalInstance();

    /** @brief Returns the default worker count used by new pools. */
    static std::size_t defaultThreadCount();

    /** @brief Returns `true` if the calling thread is a worker of any TaskPool. */
    static bool isWorkerThread();

    /**
     * @brief Queues @p runnable; it runs via `Runnable::execute()`.
     * @return `false` if @p runnable is null, the pool has no workers (web)
     *         or shutdown() has been called.
     */
    bool start(std::shared_ptr<Runnable> runnable);

    /**
     * @brief Queues @p task. An exception thrown by it is logged and dropped;
     *        the worker keeps running.
     * @return `false` if @p task is empty, the pool has no workers (web) or
     *         shutdown() has been called (also from a task still draining).
     */
    bool start(Task task);

    /**
     * @brief Blocks until no task is queued or running.
     * @throws std::logic_error if called from one of this pool's workers.
     */
    void waitForDone();

    /**
     * @brief Stops accepting tasks, runs the ones already queued, and joins
     *        the workers. Idempotent and thread-safe: every caller returns
     *        once the workers have stopped. Called by the destructor.
     *
     * Timers and socket watches of pooled nodes stop first: no timeout fires
     * and no I/O callback is queued once shutdown() has started.
     * Pooled nodes keep running while the queue drains (their queued signal
     * deliveries are processed, and they may queue more). Once the workers
     * are joined, the pooled nodes still alive are destroyed on the calling
     * thread, each as if inside its own strand. A pooled node that keeps
     * queuing work for itself without pause would keep shutdown() waiting.
     * @throws std::logic_error if called from one of this pool's workers.
     */
    void shutdown();

    /** @brief Returns the number of worker threads (0 after shutdown). */
    std::size_t maxThreadCount() const;

    /** @brief Returns the ids of the worker threads. */
    std::vector<std::thread::id> workerThreadIds() const;

    /** @brief Returns the number of tasks currently running. */
    std::size_t activeTaskCount() const;

    /** @brief Returns the number of tasks waiting for a worker. */
    std::size_t queuedTaskCount() const;

    /** @brief Returns how many strands the pool has created (in use or idle, for reuse). */
    std::size_t strandCount() const;

    /** @brief Returns how many Timers of pooled nodes are armed right now. */
    std::size_t armedTimerCount() const;

    /** @brief Returns how many descriptors of pooled nodes (sockets) are watched right now. */
    std::size_t ioWatchCount() const;

private:
    friend class detail::Strand;

    void workerLoop();
    bool isOwnWorkerThread() const;

    // Strand support. Strands are created on demand, never freed while the
    // pool lives, and recycled once idle without nodes.
    detail::Strand* acquireStrand();
    void releaseStrandReservation(detail::Strand* strand);
    void recycleStrandIfIdle(detail::Strand* strand);
    // Like start(), but also accepted while shutdown() drains the queue.
    bool enqueueStrandDrain(detail::Strand* strand);
    // Deadlines of strand-owned Timers.
    detail::TimerService& timerService() const { return *m_timerService; }
    // Descriptors of strand-owned IOEvents.
    detail::IoService& ioService() const { return *m_ioService; }

    mutable std::mutex m_mutex;
    std::mutex m_shutdownMutex;  // serialises shutdown() callers; never taken by workers
    std::condition_variable m_workAvailable;
    std::condition_variable m_done;
    std::deque<Task> m_queue;
    std::vector<std::thread> m_workers;
    std::size_t m_activeTasks = 0;
    bool m_stopping = false;
    bool m_joined = false;

    std::unique_ptr<detail::TimerService> m_timerService;
    std::unique_ptr<detail::IoService> m_ioService;

    mutable std::mutex m_strandMutex;
    std::vector<std::unique_ptr<detail::Strand>> m_strands;
    std::vector<detail::Strand*> m_freeStrands;
};

}  // namespace snf
