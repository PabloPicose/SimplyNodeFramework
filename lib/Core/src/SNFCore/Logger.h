#pragma once

/**
 * @file Logger.h
 * @brief Central thread-safe logger with async consumer thread.
 * @ingroup SNFCore_Logging
 */

#include <SNFCore/LogLevel.h>
#include <SNFCore/LogMessage.h>
#include <SNFCore/LogSink.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace snf {

/**
 * @struct LoggerStats
 * @ingroup SNFCore_Logging
 * @brief Accumulated counters for monitoring logger health.
 */
struct LoggerStats {
    std::uint64_t enqueued  = 0; ///< Total messages accepted into the queue.
    std::uint64_t processed = 0; ///< Total messages delivered to sinks.
    std::uint64_t dropped   = 0; ///< Total messages discarded due to full queue.
};

/**
 * @class Logger
 * @ingroup SNFCore_Logging
 * @brief Central, thread-safe log dispatcher with a dedicated consumer thread.
 *
 * Producer threads call `log()` (typically through the `sDebug` / `sWarning` /
 * `sCritical` macros). The call is non-blocking: the message is pushed onto an
 * internal bounded queue and control returns immediately. If the queue is full
 * the message is silently dropped and `stats().dropped` is incremented.
 *
 * A single background thread drains the queue and delivers messages to all
 * registered `LogSink` instances in insertion order (fan-out).
 *
 * **Lifecycle**
 * The Logger is normally owned by `Application` and started/stopped with it.
 * It can also be used standalone (useful in tests):
 * @code
 * snf::Logger logger;
 * logger.addSink(std::make_shared<snf::ConsoleLogSink>());
 * logger.start();
 * logger.log(snf::LogLevel::Info, "hello", __FILE__, __LINE__, __func__);
 * logger.flush();
 * logger.stop();
 * @endcode
 *
 * **Extensibility**
 * Register additional sinks with `addSink()`. All sinks are called from the
 * logger's worker thread, so they are free of producer-thread contention.
 */
class Logger
{
public:
    /// Default maximum number of messages kept in the in-flight queue.
    static constexpr std::size_t kDefaultQueueCapacity = 8192;

    /**
     * @brief Constructs the logger.
     * @param queueCapacity Maximum number of in-flight messages before drops occur.
     */
    explicit Logger(std::size_t queueCapacity = kDefaultQueueCapacity);

    ~Logger();

    // Non-copyable, non-movable.
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Starts the background consumer thread.
     *
     * Must be called before any `log()` invocation. Calling `start()` on an
     * already-running logger is a no-op.
     */
    void start();

    /**
     * @brief Stops the background consumer thread after draining the queue.
     *
     * Blocks until all messages currently in the queue have been delivered to
     * sinks and the worker thread has exited. Safe to call from any thread.
     * Calling `stop()` on a logger that has not been started is a no-op.
     */
    void stop();

    /**
     * @brief Waits until queued messages are fully delivered or timeout elapses.
     *
     * @param timeout Maximum wait duration. Pass `std::chrono::milliseconds(0)`
     *                for a non-blocking poll.
     * @return `true` if all messages accepted up to the call have been
     *         delivered to sinks before timeout.
     */
    bool flush(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

    /**
     * @brief Sets the minimum level that messages must reach to be enqueued.
     *
     * Messages below this level are discarded at the call site without
     * touching the queue. Default is `LogLevel::Debug` (all messages pass).
     *
     * Thread-safe; can be changed at any time.
     */
    void setLevel(LogLevel level) noexcept;

    /** @brief Returns the current minimum log level. */
    LogLevel level() const noexcept;

    /**
     * @brief Enqueues a log message.
     *
     * Non-blocking. If the internal queue has reached `queueCapacity` the
     * message is dropped and the `dropped` counter is incremented. Messages
     * whose level is below `level()` are filtered before reaching the queue.
     *
     * Normally called indirectly through the `sDebug` / `sInfo` / `sWarning` /
     * `sError` / `sCritical` macros defined in `Logging.h`.
     */
    void log(LogLevel     level,
             std::string  text,
             const char*  file,
             int          line,
             const char*  function);

    /**
     * @brief Registers a sink to receive log messages.
     *
     * The sink will be called from the logger's worker thread. Adding a sink
     * while the logger is running is safe (protected by an internal mutex).
     */
    void addSink(std::shared_ptr<LogSink> sink);

    /**
     * @brief Removes a previously registered sink.
     *
     * Comparison is by pointer identity. If the sink is not registered this
     * is a no-op.
     */
    void removeSink(const std::shared_ptr<LogSink>& sink);

    /** @brief Removes all registered sinks. */
    void clearSinks();

    /** @brief Returns a snapshot of the accumulated counters. */
    LoggerStats stats() const;

private:
    void workerLoop();
    void deliverToSinks(const LogMessage& message);

    const std::size_t m_queueCapacity;

    std::atomic<int>  m_levelFilter;   ///< Cast to/from LogLevel.

    mutable std::mutex         m_queueMutex;
    std::condition_variable    m_queueCv;
    std::queue<LogMessage>     m_queue;
    bool                       m_running = false;
    std::thread                m_worker;

    std::atomic<std::uint64_t> m_nextSequence{1};
    std::atomic<std::uint64_t> m_enqueued{0};
    std::atomic<std::uint64_t> m_processed{0};
    std::atomic<std::uint64_t> m_dropped{0};

    mutable std::mutex                       m_sinksMutex;
    std::vector<std::shared_ptr<LogSink>>    m_sinks;
};

}  // namespace snf
