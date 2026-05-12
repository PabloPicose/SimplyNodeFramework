#include "SNFCore/ThreadPool.h"

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/WorkerSelectionPolicy.h"

#include <algorithm>

namespace snf {

ThreadPool::ThreadPool(std::size_t maxThreadCount)
{
    m_workerSelectionPolicy = std::make_shared<DefaultWorkerSelectionPolicy>();

#ifdef SNF_PLATFORM_WEB
    // Emscripten single-threaded build: std::thread is not available without
    // -pthread.  Keep the pool as a zero-worker stub; tasks submitted via
    // start() are silently dropped (no background work is expected on web).
    (void)maxThreadCount;
#else
    maxThreadCount = std::max<std::size_t>(1, maxThreadCount);
    m_workers.reserve(maxThreadCount);
    m_workerLoops.resize(maxThreadCount, nullptr);
    for (std::size_t i = 0; i < maxThreadCount; ++i) {
        m_workers.emplace_back([this, i]() { workerLoop(i); });
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_workersReady.wait(lock, [this, maxThreadCount]() {
        return std::count_if(m_workerLoops.begin(), m_workerLoops.end(), [](EventLoop* loop) { return loop != nullptr; })
            == maxThreadCount;
    });
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

std::thread::id ThreadPool::preferredWorkerThreadId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_workerLoops.empty() || m_workerLoops.size() != m_workers.size()) {
        return std::thread::id();
    }

    std::vector<WorkerLoadSnapshot> snapshots;
    snapshots.reserve(m_workerLoops.size());
    for (std::size_t index = 0; index < m_workerLoops.size(); ++index) {
        EventLoop* loop = m_workerLoops[index];
        if (! loop) {
            continue;
        }

        WorkerLoadSnapshot snapshot;
        snapshot.threadId = m_workers[index].get_id();
        snapshot.hasPendingWork = loop->hasPendingWork();
        snapshot.pendingDeleteCount = loop->pendingDeleteCount();
        snapshot.registeredNodesCount = loop->registeredNodesCount();
        snapshot.activeTimerCount = loop->activeTimerCount();
        snapshot.lastIterationDurationNanoseconds = loop->lastIterationDurationNanoseconds();
        snapshot.lastActivityAgeNanoseconds = loop->lastActivityAgeNanoseconds();
        snapshots.push_back(snapshot);
    }

    if (snapshots.empty()) {
        return std::thread::id();
    }

    WorkerSelectionPolicyPtr policy = m_workerSelectionPolicy;
    if (! policy) {
        policy = std::make_shared<DefaultWorkerSelectionPolicy>();
    }

    const std::size_t selectedIndex = policy->selectWorkerIndex(snapshots);
    if (selectedIndex >= snapshots.size()) {
        return std::thread::id();
    }

    return snapshots[selectedIndex].threadId;
}

void ThreadPool::setWorkerSelectionPolicy(WorkerSelectionPolicyPtr policy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_workerSelectionPolicy = policy ? std::move(policy) : std::make_shared<DefaultWorkerSelectionPolicy>();
}

WorkerSelectionPolicyPtr ThreadPool::workerSelectionPolicy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_workerSelectionPolicy;
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

void ThreadPool::workerLoop(std::size_t workerIndex)
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
        if (workerIndex < m_workerLoops.size()) {
            m_workerLoops[workerIndex] = loop;
        }
    }
    m_workersReady.notify_all();

    loop->run();
}

}  // namespace snf
