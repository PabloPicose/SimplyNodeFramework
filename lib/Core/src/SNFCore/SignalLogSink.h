#pragma once

/**
 * @file SignalLogSink.h
 * @brief Log sink that re-emits messages as an SNFCore signal.
 * @ingroup SNFCore_Logging
 */

#include <SNFCore/Connection.h>
#include <SNFCore/LogMessage.h>
#include <SNFCore/LogSink.h>

namespace snf {

/**
 * @class SignalLogSink
 * @ingroup SNFCore_Logging
 * @brief Log sink that exposes a `Signal<const LogMessage&>` for each
 *        consumed message.
 *
 * This allows any part of the application to react to log events using the
 * standard signal/slot mechanism, including cross-thread delivery via
 * `ConnectionType::Queued`.
 *
 * **Usage**
 * @code
 * auto sink = std::make_shared<snf::SignalLogSink>();
 * sink->messageLogged.connect([](const snf::LogMessage& msg) {
 *     // forward to a web client, external library, etc.
 * });
 * Application::instance()->logger().addSink(sink);
 * @endcode
 *
 * @note `consume()` is invoked from the Logger's worker thread. Direct
 *       connections will run on that thread; use `ConnectionType::Queued`
 *       for receivers bound to another thread.
 */
class SignalLogSink : public LogSink
{
public:
    SignalLogSink()           = default;
    ~SignalLogSink() override = default;

    /** @brief Emitted for every log message that reaches this sink. */
    Signal<const LogMessage&> messageLogged;

    /**
     * @brief Emits `messageLogged` with @p message.
     */
    void consume(const LogMessage& message) override
    {
        messageLogged.emit(message);
    }
};

}  // namespace snf
