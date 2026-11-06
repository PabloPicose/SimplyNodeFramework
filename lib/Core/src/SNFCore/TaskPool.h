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

private:
    void workerLoop();
    bool isOwnWorkerThread() const;

    mutable std::mutex m_mutex;
    std::mutex m_shutdownMutex;  // serialises shutdown() callers; never taken by workers
    std::condition_variable m_workAvailable;
    std::condition_variable m_done;
    std::deque<Task> m_queue;
    std::vector<std::thread> m_workers;
    std::size_t m_activeTasks = 0;
    bool m_stopping = false;
};

}  // namespace snf
