#pragma once

/**
 * @file LogLevel.h
 * @brief Log severity levels and related utilities.
 * @ingroup SNFCore_Logging
 */

#include <string>

namespace snf {

/**
 * @enum LogLevel
 * @ingroup SNFCore_Logging
 * @brief Severity levels for log messages, ordered from least to most severe.
 */
enum class LogLevel : int {
    Debug    = 0,
    Info     = 1,
    Warning  = 2,
    Error    = 3,
    Critical = 4,
};

/**
 * @brief Returns the short uppercase label for a log level (e.g. "DEBUG").
 */
inline const char* logLevelToString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARNING";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

}  // namespace snf
