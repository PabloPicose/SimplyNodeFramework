#include "SNFCore/TaskPool.h"

#include "SNFCore/Application.h"
#include "SNFCore/Logging.h"
#include "SNFCore/Strand.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace snf {

namespace {

// The pool whose worker is the current thread, or null.
thread_local const TaskPool* t_currentPool = nullptr;

}  // namespace

TaskPool::TaskPool(std::size_t maxThreadCount)
{
#ifdef SNF_PLATFORM_WEB
    // Emscripten single-threaded build: no std::thread. Zero workers, so
    // start() rejects every task.
    (void)maxThreadCount;
#else
    maxThreadCount = std::max<std::size_t>(1, maxThreadCount);
    m_workers.reserve(maxThreadCount);
    for (std::size_t i = 0; i < maxThreadCount; ++i) {
        m_workers.emplace_back([this]() { workerLoop(); });
    }
#endif
}

TaskPool::~TaskPool() { shutdown(); }

TaskPool* TaskPool::globalInstance()
{
    const Application* app = Application::instance();
    return app ? app->taskPool() : nullptr;
}

std::size_t TaskPool::defaultThreadCount()
{
#ifdef SNF_PLATFORM_WEB
    return 0;
#else
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, hardwareThreads);
#endif
}

bool TaskPool::isWorkerThread() { return t_currentPool != nullptr; }

bool TaskPool::isOwnWorkerThread() const { return t_currentPool == this; }

bool TaskPool::start(std::shared_ptr<Runnable> runnable)
{
    if (! runnable) {
        return false;
    }
    return start(Task([runnable = std::move(runnable)]() { runnable->execute(); }));
}

bool TaskPool::start(Task task)
{
    if (! task) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping || m_workers.empty()) {
            return false;
        }
        m_queue.push_back(std::move(task));
    }
    m_workAvailable.notify_one();
    return true;
}

void TaskPool::waitForDone()
{
    if (isOwnWorkerThread()) {
        throw std::logic_error("TaskPool::waitForDone() called from one of its own workers");
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [this]() { return m_queue.empty() && m_activeTasks == 0; });
}

void TaskPool::shutdown()
{
    if (isOwnWorkerThread()) {
        throw std::logic_error("TaskPool::shutdown() called from one of its own workers");
    }

    // Held for the whole call: a concurrent caller waits here until the
    // first one has joined the workers, so every caller returns with the
    // pool stopped (not only the one that took the workers).
    std::lock_guard<std::mutex> shutdownLock(m_shutdownMutex);

    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        workers.swap(m_workers);
    }
    m_workAvailable.notify_all();

    // Workers drain the queue before leaving (see workerLoop()). Each one is
    // joined exactly once: the list was taken out of m_workers above.
    for (std::thread& worker : workers) {
        worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_joined = true;
    }

    // No worker can run a strand any more: destroy the pooled nodes left.
    std::vector<detail::Strand*> strands;
    {
        std::lock_guard<std::mutex> lock(m_strandMutex);
        strands.resize(m_strands.size());
        std::transform(m_strands.begin(), m_strands.end(), strands.begin(),
                       [](const std::unique_ptr<detail::Strand>& strand) { return strand.get(); });
    }
    for (detail::Strand* strand : strands) {
        strand->destroyRemainingNodes();
    }
}

std::size_t TaskPool::strandCount() const
{
    std::lock_guard<std::mutex> lock(m_strandMutex);
    return m_strands.size();
}

detail::Strand* TaskPool::acquireStrand()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping || m_workers.empty()) {
            return nullptr;
        }
    }
    std::lock_guard<std::mutex> lock(m_strandMutex);
    detail::Strand* strand = nullptr;
    if (! m_freeStrands.empty()) {
        strand = m_freeStrands.back();
        m_freeStrands.pop_back();
    } else {
        m_strands.push_back(std::make_unique<detail::Strand>(*this));
        strand = m_strands.back().get();
    }
    strand->m_free = false;
    strand->m_reserved = true;
    return strand;
}

void TaskPool::releaseStrandReservation(detail::Strand* strand)
{
    std::lock_guard<std::mutex> lock(m_strandMutex);
    strand->m_reserved = false;
    if (strand->isRecyclable()) {
        strand->m_free = true;
        m_freeStrands.push_back(strand);
    }
}

void TaskPool::recycleStrandIfIdle(detail::Strand* strand)
{
    std::lock_guard<std::mutex> lock(m_strandMutex);
    if (strand->isRecyclable()) {
        strand->m_free = true;
        m_freeStrands.push_back(strand);
    }
}

bool TaskPool::enqueueStrandDrain(detail::Strand* strand)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_joined || (m_workers.empty() && ! m_stopping)) {
            return false;
        }
        m_queue.push_back([strand]() { strand->drain(); });
    }
    m_workAvailable.notify_one();
    return true;
}

std::size_t TaskPool::maxThreadCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_workers.size();
}

std::vector<std::thread::id> TaskPool::workerThreadIds() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::thread::id> ids(m_workers.size());
    std::transform(m_workers.begin(), m_workers.end(), ids.begin(), [](const std::thread& worker) {
        return worker.get_id();
    });
    return ids;
}

std::size_t TaskPool::activeTaskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeTasks;
}

std::size_t TaskPool::queuedTaskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void TaskPool::workerLoop()
{
    t_currentPool = this;

    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workAvailable.wait(lock, [this]() { return m_stopping || ! m_queue.empty(); });
            if (m_queue.empty()) {
                break;  // stopping and nothing left to run
            }
            task = std::move(m_queue.front());
            m_queue.pop_front();
            ++m_activeTasks;
        }

        try {
            task();
        } catch (const std::exception& error) {
            sError() << "TaskPool: task threw an exception: " << error.what();
        } catch (...) {
            sError() << "TaskPool: task threw a non-standard exception";
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_activeTasks;
            if (m_queue.empty() && m_activeTasks == 0) {
                m_done.notify_all();
            }
        }
    }

    t_currentPool = nullptr;
}

}  // namespace snf
