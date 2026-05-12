#pragma once

/**
 * @file ThreadPool.h
 * @brief Fixed-size worker pool for Runnable execution.
 * @ingroup SNFCore
 */

#include <SNFCore/Runnable.h>
#include <SNFCore/WorkerSelectionPolicy.h>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace snf {

class EventLoop;

/**
 * @class ThreadPool
 * @ingroup SNFCore
 * @brief Executes Runnable instances on a bounded set of worker threads.
 */
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t maxThreadCount = defaultThreadCount());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Returns the Application-owned global ThreadPool, if an Application exists.
     */
    static ThreadPool* globalInstance();

    /** @brief Returns the default worker count used by new pools. */
    static std::size_t defaultThreadCount();

    /** @brief Queues @p runnable for execution. */
    bool start(std::shared_ptr<Runnable> runnable);

    /** @brief Blocks until all currently queued/running tasks are complete. */
    void waitForDone();

    /** @brief Stops workers after queued tasks finish. Called automatically by the destructor. */
    void shutdown();

    /** @brief Returns the configured maximum number of worker threads. */
    std::size_t maxThreadCount() const;

    /** @brief Returns the ids of the worker threads owned by this pool. */
    std::vector<std::thread::id> workerThreadIds() const;

    /** @brief Returns the worker thread that appears least loaded right now. */
    std::thread::id preferredWorkerThreadId() const;

    /** @brief Sets the worker selection policy used by preferredWorkerThreadId(). */
    void setWorkerSelectionPolicy(WorkerSelectionPolicyPtr policy);

    /** @brief Returns the current worker selection policy. */
    WorkerSelectionPolicyPtr workerSelectionPolicy() const;

    /** @brief Returns the number of tasks currently executing. */
    std::size_t activeThreadCount() const;

    /** @brief Returns the number of queued tasks waiting for a worker. */
    std::size_t queuedTaskCount() const;

private:
    void workerLoop(std::size_t workerIndex);

    mutable std::mutex m_mutex;
    std::condition_variable m_workersReady;
    std::condition_variable m_done;
    std::vector<std::thread> m_workers;
    std::vector<EventLoop*> m_workerLoops;
    std::size_t m_nextWorker = 0;
    std::size_t m_queuedTasks = 0;
    std::size_t m_activeTasks = 0;
    bool m_stopping = false;
    WorkerSelectionPolicyPtr m_workerSelectionPolicy;
};

}  // namespace snf
