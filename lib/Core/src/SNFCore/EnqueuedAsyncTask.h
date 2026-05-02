#pragma once

/**
 * @file EnqueuedAsyncTask.h
 * @brief Dependency graph scheduler for AsyncTask instances.
 * @ingroup SNFCore
 */

#include <SNFCore/AsyncTask.h>
#include <SNFCore/Connection.h>
#include <SNFCore/ThreadPool.h>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace snf {

/**
 * @class EnqueuedAsyncTask
 * @ingroup SNFCore
 * @brief Runs a directed acyclic graph of AsyncTask instances on a ThreadPool.
 *
 * Dependencies are expressed as "dependency must finish before dependent".
 * The graph is valid only when it is acyclic and every path reaches one
 * unique exit task.
 */
class EnqueuedAsyncTask
{
public:
    using TaskId = std::size_t;
    static constexpr TaskId invalidTaskId = static_cast<TaskId>(-1);

    EnqueuedAsyncTask();
    ~EnqueuedAsyncTask();

    EnqueuedAsyncTask(const EnqueuedAsyncTask&) = delete;
    EnqueuedAsyncTask& operator=(const EnqueuedAsyncTask&) = delete;

    /** @brief Adds a task node and returns its id, or invalidTaskId on error. */
    TaskId addTask(std::shared_ptr<AsyncTask> task);

    /** @brief Makes @p dependent wait for @p dependency. */
    bool addDependency(TaskId dependency, TaskId dependent);

    /** @brief Returns whether the task graph is acyclic and has one exit task. */
    bool hasValidSingleExit() const;

    /** @brief Starts all ready tasks on @p pool, or ThreadPool::globalInstance(). */
    bool start(ThreadPool* pool = ThreadPool::globalInstance());

    /** @brief Blocks until the graph has finished or is not running. */
    void wait();

    bool isRunning() const;
    bool isFinished() const;
    std::size_t taskCount() const;
    std::size_t finishedTaskCount() const;

    /** @brief Emitted once when all tasks have finished. */
    Signal<> finished;

    /** @brief Emitted whenever an individual task node finishes. */
    Signal<TaskId> taskFinished;

private:
    struct TaskNode
    {
        std::shared_ptr<AsyncTask> task;
        std::vector<TaskId> dependents;
        std::vector<TaskId> dependencies;
        std::size_t pendingDependencies = 0;
        bool scheduled = false;
        bool done = false;
    };

    bool isValidTaskId(TaskId id) const;
    bool hasValidSingleExitLocked() const;
    void scheduleTask(TaskId id);
    void onTaskFinished(TaskId id);

    mutable std::mutex m_mutex;
    std::condition_variable m_done;
    std::vector<TaskNode> m_tasks;
    ThreadPool* m_pool = nullptr;
    std::size_t m_remainingTasks = 0;
    std::size_t m_finishedTasks = 0;
    bool m_running = false;
    bool m_finished = false;
};

}  // namespace snf
