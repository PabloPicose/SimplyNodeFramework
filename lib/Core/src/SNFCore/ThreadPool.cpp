#include "SNFCore/ThreadPool.h"

#include "SNFCore/Application.h"

#include <algorithm>

namespace snf {

ThreadPool::ThreadPool(std::size_t maxThreadCount)
{
    maxThreadCount = std::max<std::size_t>(1, maxThreadCount);
    m_workers.reserve(maxThreadCount);
    for (std::size_t i = 0; i < maxThreadCount; ++i) {
        m_workers.emplace_back([this]() { workerLoop(); });
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

ThreadPool* ThreadPool::globalInstance()
{
    Application* app = Application::instance();
    return app ? app->threadPool() : nullptr;
}

std::size_t ThreadPool::defaultThreadCount()
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, hardwareThreads == 0 ? 2 : hardwareThreads);
}

bool ThreadPool::start(std::shared_ptr<Runnable> runnable)
{
    if (! runnable) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            return false;
        }
        m_tasks.push(std::move(runnable));
    }

    m_workAvailable.notify_one();
    return true;
}

void ThreadPool::waitForDone()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [this]() { return m_tasks.empty() && m_activeTasks == 0; });
}

void ThreadPool::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            return;
        }
        m_stopping = true;
    }

    m_workAvailable.notify_all();
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
}

std::size_t ThreadPool::maxThreadCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_workers.size();
}

std::vector<std::thread::id> ThreadPool::workerThreadIds() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::thread::id> ids;
    ids.reserve(m_workers.size());
    for (const std::thread& worker : m_workers) {
        ids.push_back(worker.get_id());
    }
    return ids;
}

std::size_t ThreadPool::activeThreadCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeTasks;
}

std::size_t ThreadPool::queuedTaskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

void ThreadPool::workerLoop()
{
    while (true) {
        std::shared_ptr<Runnable> runnable;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workAvailable.wait(lock, [this]() { return m_stopping || ! m_tasks.empty(); });
            if (m_stopping && m_tasks.empty()) {
                return;
            }

            runnable = std::move(m_tasks.front());
            m_tasks.pop();
            ++m_activeTasks;
        }

        runnable->execute();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_activeTasks;
            if (m_tasks.empty() && m_activeTasks == 0) {
                m_done.notify_all();
            }
        }
    }
}

}  // namespace snf
