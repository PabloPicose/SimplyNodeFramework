#include "SNFCore/ThreadPool.h"

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"

#include <algorithm>

namespace snf {

ThreadPool::ThreadPool(std::size_t maxThreadCount)
{
#ifdef SNF_PLATFORM_WEB
    // Emscripten single-threaded build: std::thread is not available without
    // -pthread.  Keep the pool as a zero-worker stub; tasks submitted via
    // start() are silently dropped (no background work is expected on web).
    (void)maxThreadCount;
#else
    maxThreadCount = std::max<std::size_t>(1, maxThreadCount);
    m_workers.reserve(maxThreadCount);
    for (std::size_t i = 0; i < maxThreadCount; ++i) {
        m_workers.emplace_back([this]() { workerLoop(); });
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_workersReady.wait(lock, [this, maxThreadCount]() { return m_workerLoops.size() == maxThreadCount; });
#endif
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
#ifdef SNF_PLATFORM_WEB
    return 0;  // No worker threads in Emscripten single-threaded builds.
#else
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, hardwareThreads == 0 ? 2 : hardwareThreads);
#endif
}

bool ThreadPool::start(std::shared_ptr<Runnable> runnable)
{
    if (! runnable) {
        return false;
    }

    EventLoop* targetLoop = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping || m_workerLoops.empty()) {
            return false;
        }
        targetLoop = m_workerLoops[m_nextWorker % m_workerLoops.size()];
        ++m_nextWorker;
        ++m_queuedTasks;
    }

    targetLoop->post([this, runnable = std::move(runnable)]() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_queuedTasks;
            ++m_activeTasks;
        }

        runnable->execute();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_activeTasks;
            if (m_queuedTasks == 0 && m_activeTasks == 0) {
                m_done.notify_all();
            }
        }
    });
    return true;
}

void ThreadPool::waitForDone()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [this]() { return m_queuedTasks == 0 && m_activeTasks == 0; });
}

void ThreadPool::shutdown()
{
    std::vector<EventLoop*> workerLoops;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            return;
        }
        m_stopping = true;
        workerLoops = m_workerLoops;
    }

    for (EventLoop* loop : workerLoops) {
        if (loop) {
            loop->stop();
        }
    }

    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_workers.clear();
        m_workerLoops.clear();
        m_nextWorker = 0;
    }
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
    return m_queuedTasks;
}

void ThreadPool::workerLoop()
{
    std::unique_ptr<EventLoop> standaloneLoop;
    EventLoop* loop = nullptr;

    if (Application* app = Application::instance()) {
        loop = app->getOrCreateCurrentThreadEventLoop();
    } else {
        standaloneLoop = std::make_unique<EventLoop>();
        loop = standaloneLoop.get();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_workerLoops.push_back(loop);
    }
    m_workersReady.notify_all();

    loop->run();
}

}  // namespace snf
