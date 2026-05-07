#pragma once

/**
 * @file LogMessage.h
 * @brief Structured log message type carrying all contextual metadata.
 * @ingroup SNFCore_Logging
 */

#include <SNFCore/LogLevel.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace snf {

/**
 * @struct LogMessage
 * @ingroup SNFCore_Logging
 * @brief Immutable value object representing a single log entry.
 *
 * All fields are populated at the point of emission (inside the
 * `LogEntryBuilder` destructor) and are immutable thereafter.
 */
struct LogMessage {
    /// Monotonically increasing sequence number assigned by the Logger.
    std::uint64_t sequence = 0;

    /// Wall-clock time at which the log entry was produced.
    std::chrono::system_clock::time_point timestamp;

    /// Severity level.
    LogLevel level = LogLevel::Debug;

    /// Formatted message text built by the stream builder.
    std::string text;

    /// Source file name (__FILE__).
    const char* file = "";

    /// Source line number (__LINE__).
    int line = 0;

    /// Source function name (__func__).
    const char* function = "";

    /// ID of the thread that produced the entry.
    std::thread::id threadId;
};

}  // namespace snf
