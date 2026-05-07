#pragma once

/**
 * @file ConsoleLogSink.h
 * @brief Log sink that writes formatted messages to stderr.
 * @ingroup SNFCore_Logging
 */

#include <SNFCore/LogSink.h>

namespace snf {

/**
 * @class ConsoleLogSink
 * @ingroup SNFCore_Logging
 * @brief Default log sink: writes one line per message to stderr.
 *
 * Output format (fixed columns, space-separated):
 * @code
 * <ISO-8601 timestamp> [<LEVEL>] (<thread-id>) <file>:<line> <func>: <text>
 * @endcode
 *
 * `consume()` is called exclusively from the `Logger` worker thread,
 * so no additional synchronisation is needed here.
 */
class ConsoleLogSink : public LogSink
{
public:
    ConsoleLogSink()          = default;
    ~ConsoleLogSink() override = default;

    /**
     * @brief Formats @p message and writes it to stderr.
     */
    void consume(const LogMessage& message) override;
};

}  // namespace snf
