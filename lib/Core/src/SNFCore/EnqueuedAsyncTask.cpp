#include "SNFCore/EnqueuedAsyncTask.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

namespace snf {
namespace {

class GraphTask final : public AsyncTask
{
public:
    GraphTask(std::shared_ptr<AsyncTask> task, std::function<void()> onFinished)
        : m_task(std::move(task)), m_onFinished(std::move(onFinished))
    {
    }

protected:
    void run() override
    {
        if (m_task) {
            m_task->execute();
        }
        if (m_onFinished) {
            m_onFinished();
        }
    }

private:
    std::shared_ptr<AsyncTask> m_task;
    std::function<void()> m_onFinished;
};

}  // namespace

EnqueuedAsyncTask::EnqueuedAsyncTask() = default;

EnqueuedAsyncTask::~EnqueuedAsyncTask()
{
    wait();
}

EnqueuedAsyncTask::TaskId EnqueuedAsyncTask::addTask(std::shared_ptr<AsyncTask> task)
{
    if (! task) {
        return invalidTaskId;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return invalidTaskId;
    }

    const TaskId id = m_tasks.size();
    m_tasks.push_back(TaskNode{std::move(task)});
    return id;
}

bool EnqueuedAsyncTask::addDependency(TaskId dependency, TaskId dependent)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running || ! isValidTaskId(dependency) || ! isValidTaskId(dependent) || dependency == dependent) {
        return false;
    }

    auto& dependents = m_tasks[dependency].dependents;
    if (std::find(dependents.begin(), dependents.end(), dependent) != dependents.end()) {
        return true;
    }

    dependents.push_back(dependent);
    m_tasks[dependent].dependencies.push_back(dependency);
    return true;
}

bool EnqueuedAsyncTask::hasValidSingleExit() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return hasValidSingleExitLocked();
}

bool EnqueuedAsyncTask::start(ThreadPool* pool)
{
    std::vector<TaskId> readyTasks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running || ! pool || ! hasValidSingleExitLocked()) {
            return false;
        }

        m_pool = pool;
        m_running = true;
        m_finished = false;
        m_remainingTasks = m_tasks.size();
        m_finishedTasks = 0;

        for (TaskId id = 0; id < m_tasks.size(); ++id) {
            TaskNode& node = m_tasks[id];
            node.pendingDependencies = node.dependencies.size();
            node.scheduled = false;
            node.done = false;
            if (node.pendingDependencies == 0) {
                node.scheduled = true;
                readyTasks.push_back(id);
            }
        }
    }

    for (const TaskId id : readyTasks) {
        scheduleTask(id);
    }
    return true;
}

void EnqueuedAsyncTask::wait()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [this]() { return ! m_running; });
}

bool EnqueuedAsyncTask::isRunning() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
}

bool EnqueuedAsyncTask::isFinished() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_finished;
}

std::size_t EnqueuedAsyncTask::taskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

std::size_t EnqueuedAsyncTask::finishedTaskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_finishedTasks;
}

bool EnqueuedAsyncTask::isValidTaskId(TaskId id) const
{
    return id < m_tasks.size();
}

bool EnqueuedAsyncTask::hasValidSingleExitLocked() const
{
    if (m_tasks.empty()) {
        return false;
    }

    TaskId exitTask = invalidTaskId;
    std::size_t exitCount = 0;
    std::vector<std::size_t> incoming(m_tasks.size(), 0);
    for (TaskId id = 0; id < m_tasks.size(); ++id) {
        if (m_tasks[id].dependents.empty()) {
            exitTask = id;
            ++exitCount;
        }
        for (const TaskId dependent : m_tasks[id].dependents) {
            if (dependent >= m_tasks.size()) {
                return false;
            }
            ++incoming[dependent];
        }
    }

    if (exitCount != 1 || exitTask == invalidTaskId) {
        return false;
    }

    std::queue<TaskId> ready;
    for (TaskId id = 0; id < incoming.size(); ++id) {
        if (incoming[id] == 0) {
            ready.push(id);
        }
    }

    std::size_t visited = 0;
    while (! ready.empty()) {
        const TaskId id = ready.front();
        ready.pop();
        ++visited;

        for (const TaskId dependent : m_tasks[id].dependents) {
            --incoming[dependent];
            if (incoming[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }

    if (visited != m_tasks.size()) {
        return false;
    }

    for (TaskId start = 0; start < m_tasks.size(); ++start) {
        std::vector<bool> seen(m_tasks.size(), false);
        std::queue<TaskId> pending;
        pending.push(start);
        seen[start] = true;
        bool reachesExit = false;

        while (! pending.empty()) {
            const TaskId id = pending.front();
            pending.pop();
            if (id == exitTask) {
                reachesExit = true;
                break;
            }

            for (const TaskId dependent : m_tasks[id].dependents) {
                if (! seen[dependent]) {
                    seen[dependent] = true;
                    pending.push(dependent);
                }
            }
        }

        if (! reachesExit) {
            return false;
        }
    }

    return true;
}

void EnqueuedAsyncTask::scheduleTask(TaskId id)
{
    std::shared_ptr<AsyncTask> task;
    ThreadPool* pool = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (! m_running || ! isValidTaskId(id)) {
            return;
        }
        task = m_tasks[id].task;
        pool = m_pool;
    }

    if (! pool || ! pool->start(std::make_shared<GraphTask>(std::move(task), [this, id]() { onTaskFinished(id); }))) {
        onTaskFinished(id);
    }
}

void EnqueuedAsyncTask::onTaskFinished(TaskId id)
{
    std::vector<TaskId> readyTasks;
    bool graphFinished = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (! m_running || ! isValidTaskId(id) || m_tasks[id].done) {
            return;
        }

        m_tasks[id].done = true;
        ++m_finishedTasks;
        --m_remainingTasks;

        for (const TaskId dependent : m_tasks[id].dependents) {
            TaskNode& node = m_tasks[dependent];
            if (node.pendingDependencies > 0) {
                --node.pendingDependencies;
            }
            if (node.pendingDependencies == 0 && ! node.scheduled) {
                node.scheduled = true;
                readyTasks.push_back(dependent);
            }
        }

        if (m_remainingTasks == 0) {
            m_running = false;
            m_finished = true;
            graphFinished = true;
        }
    }

    taskFinished.emit(id);

    for (const TaskId readyTask : readyTasks) {
        scheduleTask(readyTask);
    }

    if (graphFinished) {
        m_done.notify_all();
        finished.emit();
    }
}

}  // namespace snf
