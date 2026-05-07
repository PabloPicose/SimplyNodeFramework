#pragma once

/**
 * @file LogSink.h
 * @brief Abstract contract for log output destinations.
 * @ingroup SNFCore_Logging
 */

#include <SNFCore/LogMessage.h>

namespace snf {

/**
 * @class LogSink
 * @ingroup SNFCore_Logging
 * @brief Interface for log output destinations (sinks).
 *
 * Concrete sinks (console, file, network, signal) inherit this class and
 * implement `consume()`. The `Logger` calls `consume()` exclusively from
 * its own worker thread, so implementations do not need internal locking
 * for single-threaded consumers. Cross-thread delivery must be handled
 * by the sink itself if required (e.g. via `SignalLogSink`).
 *
 * Ownership is shared: sinks are registered with `std::shared_ptr<LogSink>`
 * so they can outlive their registration if held externally.
 */
class LogSink
{
public:
    virtual ~LogSink() = default;

    /**
     * @brief Called by the Logger worker thread for each log message.
     *
     * @param message The log message to output. The message is passed by
     *                const reference; do not store a reference — copy the
     *                struct if retention is needed.
     *
     * @note This method is always called from the Logger's private worker
     *       thread, never from the producer thread.
     */
    virtual void consume(const LogMessage& message) = 0;
};

}  // namespace snf
