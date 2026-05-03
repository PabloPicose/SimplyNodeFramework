#pragma once

/**
 * @file AsyncTaskSequence.h
 * @brief Dependency graph scheduler for AsyncTask instances.
 * @ingroup SNFExperimental
 */

#include <SNFCore/Connection.h>
#include <SNFCore/ThreadPool.h>
#include <SNFExperimental/AsyncTask.h>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace snf {

/**
 * @class AsyncTaskSequence
 * @ingroup SNFExperimental
 * @brief Runs a directed acyclic graph of AsyncTask nodes on a ThreadPool.
 *
 * Dependencies are expressed as "dependency must finish before dependent".
 * Multiple roots and multiple terminal tasks are valid. The sequence emits
 * finished() when every scheduled task has completed.
 */
class AsyncTaskSequence
{
public:
    using TaskId = std::size_t;
    static constexpr TaskId invalidTaskId = static_cast<TaskId>(-1);

    AsyncTaskSequence();
    ~AsyncTaskSequence();

    AsyncTaskSequence(const AsyncTaskSequence&) = delete;
    AsyncTaskSequence& operator=(const AsyncTaskSequence&) = delete;

    TaskId addTask(std::shared_ptr<AsyncTask> task);
    bool addDependency(TaskId dependency, TaskId dependent);

    /** @brief Returns whether the graph is non-empty and acyclic. */
    bool hasValidGraph() const;

    bool start(ThreadPool* pool = ThreadPool::globalInstance());
    void wait();

    bool isRunning() const;
    bool isFinished() const;
    std::size_t taskCount() const;
    std::size_t finishedTaskCount() const;

    /** @brief Returns the output context produced by @p taskId. */
    AsyncTaskContext output(TaskId taskId) const;

    Signal<> finished;
    Signal<TaskId> taskFinished;

private:
    struct TaskNode
    {
        std::shared_ptr<AsyncTask> task;
        std::vector<TaskId> dependents;
        std::vector<TaskId> dependencies;
        AsyncTaskContext output;
        std::size_t pendingDependencies = 0;
        bool scheduled = false;
        bool done = false;
    };

    bool isValidTaskId(TaskId id) const;
    bool hasValidGraphLocked() const;
    AsyncTaskContext buildInputForTaskLocked(TaskId id) const;
    void scheduleTask(TaskId id);
    void onTaskFinished(TaskId id, AsyncTaskContext output);

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
