#pragma once

/**
 * @file ConsoleLogSink.h
 * @brief Log sink that writes formatted messages to stderr.
 * @ingroup SNFCore_Logging
 */

#include <SNFCore/LogSink.h>

#include <mutex>

namespace snf {

/**
 * @class ConsoleLogSink
 * @ingroup SNFCore_Logging
 * @brief Default log sink: writes one line per message to stderr.
 *
 * By default, it prints only level and text:
 * @code
 * [INFO] service started
 * @endcode
 *
 * `consume()` is called from the Logger worker thread. Option updates are
 * thread-safe and can be changed at runtime.
 */
class ConsoleLogSink : public LogSink
{
public:
    /**
     * @struct Options
     * @brief Formatting flags for ConsoleLogSink output.
     */
    struct Options {
        bool showTimestamp = false;
        bool showThreadId = false;
        bool showFilePath = false;
        bool showLine = false;
        bool showFunction = false;
    };

    ConsoleLogSink();
    explicit ConsoleLogSink(const Options& options);
    ~ConsoleLogSink() override = default;

    /** @brief Updates formatting options. Thread-safe. */
    void setOptions(const Options& options);

    /** @brief Returns current formatting options. Thread-safe. */
    Options options() const;

    /**
     * @brief Formats @p message and writes it to stderr.
     */
    void consume(const LogMessage& message) override;

private:
    mutable std::mutex m_optionsMutex;
    Options m_options;
};

}  // namespace snf
