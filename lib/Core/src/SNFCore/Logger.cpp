#include "SNFCore/Logger.h"

#include <algorithm>

namespace snf {

Logger::Logger(std::size_t queueCapacity)
    : m_queueCapacity(queueCapacity)
    , m_levelFilter(static_cast<int>(LogLevel::Debug))
{
}

Logger::~Logger()
{
    stop();
}

void Logger::start()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_running) {
        return;
    }
    m_running = true;
    m_worker  = std::thread([this]() { workerLoop(); });
}

void Logger::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (!m_running) {
            return;
        }
        m_running = false;
    }
    m_queueCv.notify_one();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool Logger::flush(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(m_queueMutex);
    return m_queueCv.wait_until(lock, deadline, [this]() { return m_queue.empty(); });
}

void Logger::setLevel(LogLevel lv) noexcept
{
    m_levelFilter.store(static_cast<int>(lv), std::memory_order_relaxed);
}

LogLevel Logger::level() const noexcept
{
    return static_cast<LogLevel>(m_levelFilter.load(std::memory_order_relaxed));
}

void Logger::log(LogLevel     lv,
                 std::string  text,
                 const char*  file,
                 int          line,
                 const char*  function)
{
    // Fast path: discard below minimum level without touching the queue.
    if (static_cast<int>(lv) < m_levelFilter.load(std::memory_order_relaxed)) {
        return;
    }

    LogMessage msg;
    msg.sequence  = m_nextSequence.fetch_add(1, std::memory_order_relaxed);
    msg.timestamp = std::chrono::system_clock::now();
    msg.level     = lv;
    msg.text      = std::move(text);
    msg.file      = file;
    msg.line      = line;
    msg.function  = function;
    msg.threadId  = std::this_thread::get_id();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.size() >= m_queueCapacity) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        m_queue.push(std::move(msg));
        m_enqueued.fetch_add(1, std::memory_order_relaxed);
    }
    m_queueCv.notify_one();
}

void Logger::addSink(std::shared_ptr<LogSink> sink)
{
    if (!sink) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_sinksMutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::removeSink(const std::shared_ptr<LogSink>& sink)
{
    std::lock_guard<std::mutex> lock(m_sinksMutex);
    m_sinks.erase(std::remove(m_sinks.begin(), m_sinks.end(), sink), m_sinks.end());
}

void Logger::clearSinks()
{
    std::lock_guard<std::mutex> lock(m_sinksMutex);
    m_sinks.clear();
}

LoggerStats Logger::stats() const
{
    return {
        m_enqueued.load(std::memory_order_relaxed),
        m_processed.load(std::memory_order_relaxed),
        m_dropped.load(std::memory_order_relaxed),
    };
}

// ── Private ──────────────────────────────────────────────────────────────────

void Logger::workerLoop()
{
    while (true) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_queueCv.wait(lock, [this]() { return !m_queue.empty() || !m_running; });

        // Drain the entire queue in this wakeup to amortise lock cost.
        while (!m_queue.empty()) {
            LogMessage msg = std::move(m_queue.front());
            m_queue.pop();
            lock.unlock();

            deliverToSinks(msg);
            m_processed.fetch_add(1, std::memory_order_relaxed);

            lock.lock();
        }

        // Notify flush() waiters that the queue is empty.
        m_queueCv.notify_all();

        if (!m_running) {
            break;
        }
    }
}

void Logger::deliverToSinks(const LogMessage& message)
{
    std::vector<std::shared_ptr<LogSink>> sinks;
    {
        std::lock_guard<std::mutex> lock(m_sinksMutex);
        sinks = m_sinks;   // Snapshot so sinks can be added/removed concurrently.
    }
    for (const auto& sink : sinks) {
        sink->consume(message);
    }
}

}  // namespace snf
