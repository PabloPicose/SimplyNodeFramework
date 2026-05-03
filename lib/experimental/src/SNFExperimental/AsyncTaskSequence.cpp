#include "SNFExperimental/AsyncTaskSequence.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

namespace snf {
namespace {

class SequenceRunnable final : public Runnable
{
public:
    SequenceRunnable(std::shared_ptr<AsyncTask> task,
                     AsyncTaskContext input,
                     std::function<void(AsyncTaskContext)> onFinished)
        : m_task(std::move(task)), m_input(std::move(input)), m_onFinished(std::move(onFinished))
    {
    }

protected:
    void run() override
    {
        AsyncTaskContext output;
        if (m_task) {
            output = m_task->execute(m_input);
        }
        if (m_onFinished) {
            m_onFinished(std::move(output));
        }
    }

private:
    std::shared_ptr<AsyncTask> m_task;
    AsyncTaskContext m_input;
    std::function<void(AsyncTaskContext)> m_onFinished;
};

}  // namespace

AsyncTaskSequence::AsyncTaskSequence() = default;

AsyncTaskSequence::~AsyncTaskSequence()
{
    wait();
}

AsyncTaskSequence::TaskId AsyncTaskSequence::addTask(std::shared_ptr<AsyncTask> task)
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

bool AsyncTaskSequence::addDependency(TaskId dependency, TaskId dependent)
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

bool AsyncTaskSequence::hasValidGraph() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return hasValidGraphLocked();
}

bool AsyncTaskSequence::start(ThreadPool* pool)
{
    std::vector<TaskId> readyTasks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running || ! pool || ! hasValidGraphLocked()) {
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
            node.output = AsyncTaskContext();
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

void AsyncTaskSequence::wait()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [this]() { return ! m_running; });
}

bool AsyncTaskSequence::isRunning() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
}

bool AsyncTaskSequence::isFinished() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_finished;
}

std::size_t AsyncTaskSequence::taskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

std::size_t AsyncTaskSequence::finishedTaskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_finishedTasks;
}

AsyncTaskContext AsyncTaskSequence::output(TaskId taskId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return isValidTaskId(taskId) ? m_tasks[taskId].output : AsyncTaskContext();
}

bool AsyncTaskSequence::isValidTaskId(TaskId id) const
{
    return id < m_tasks.size();
}

bool AsyncTaskSequence::hasValidGraphLocked() const
{
    if (m_tasks.empty()) {
        return false;
    }

    std::vector<std::size_t> incoming(m_tasks.size(), 0);
    for (TaskId id = 0; id < m_tasks.size(); ++id) {
        for (const TaskId dependent : m_tasks[id].dependents) {
            if (dependent >= m_tasks.size()) {
                return false;
            }
            ++incoming[dependent];
        }
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

    return visited == m_tasks.size();
}

AsyncTaskContext AsyncTaskSequence::buildInputForTaskLocked(TaskId id) const
{
    AsyncTaskContext input;
    if (! isValidTaskId(id)) {
        return input;
    }

    for (const TaskId dependency : m_tasks[id].dependencies) {
        const AsyncTaskContext& dependencyOutput = m_tasks[dependency].output;
        input.setDependencyOutput(dependency, dependencyOutput);
        input.mergeFrom(dependencyOutput);
    }

    return input;
}

void AsyncTaskSequence::scheduleTask(TaskId id)
{
    std::shared_ptr<AsyncTask> task;
    AsyncTaskContext input;
    ThreadPool* pool = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (! m_running || ! isValidTaskId(id)) {
            return;
        }
        task = m_tasks[id].task;
        input = buildInputForTaskLocked(id);
        pool = m_pool;
    }

    auto runnable = std::make_shared<SequenceRunnable>(
        std::move(task),
        std::move(input),
        [this, id](AsyncTaskContext output) { onTaskFinished(id, std::move(output)); });

    if (! pool || ! pool->start(std::move(runnable))) {
        onTaskFinished(id, AsyncTaskContext());
    }
}

void AsyncTaskSequence::onTaskFinished(TaskId id, AsyncTaskContext output)
{
    std::vector<TaskId> readyTasks;
    bool graphFinished = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (! m_running || ! isValidTaskId(id) || m_tasks[id].done) {
            return;
        }

        m_tasks[id].output = std::move(output);
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
