#pragma once

/**
 * @file Logging.h
 * @brief Stream-style logging macros: sDebug, sInfo, sWarning, sError, sCritical.
 * @ingroup SNFCore_Logging
 *
 * **Usage**
 * @code
 * #include <SNFCore/Logging.h>
 *
 * sDebug()    << "value=" << 42;
 * sWarning()  << "retrying, attempt " << n;
 * sCritical() << "fatal: " << errorMessage;
 * @endcode
 *
 * Each macro constructs a temporary `LogEntryBuilder`. Text is accumulated
 * via `operator<<`, and when the temporary is destroyed (end of statement)
 * the assembled string is submitted to `Application::instance()->logger()`.
 * If no `Application` exists the message is silently discarded.
 */

#include <SNFCore/LogLevel.h>
#include <SNFCore/Logger.h>

#include <sstream>
#include <string>
#include <utility>

namespace snf {

/**
 * @class LogEntryBuilder
 * @ingroup SNFCore_Logging
 * @brief RAII helper that accumulates a log message through `operator<<` and
 *        submits it to the central logger on destruction.
 *
 * Do not construct this class directly; use the `sDebug` / `sInfo` /
 * `sWarning` / `sError` / `sCritical` macros.
 */
class LogEntryBuilder
{
public:
    LogEntryBuilder(Logger*     logger,
                    LogLevel    level,
                    const char* file,
                    int         line,
                    const char* function)
        : m_logger(logger)
        , m_level(level)
        , m_file(file)
        , m_line(line)
        , m_function(function)
    {
    }

    ~LogEntryBuilder()
    {
        if (m_logger) {
            m_logger->log(m_level, m_stream.str(), m_file, m_line, m_function);
        }
    }

    // Non-copyable; move semantics for perfect forwarding inside the macros.
    LogEntryBuilder(const LogEntryBuilder&)            = delete;
    LogEntryBuilder& operator=(const LogEntryBuilder&) = delete;
    LogEntryBuilder(LogEntryBuilder&&)                 = default;

    /**
     * @brief Appends @p value to the message being built.
     *
     * Returns `*this` to allow chaining: `sDebug() << "x=" << x`.
     */
    template <typename T>
    LogEntryBuilder& operator<<(T&& value)
    {
        m_stream << std::forward<T>(value);
        return *this;
    }

private:
    Logger*           m_logger;
    LogLevel          m_level;
    const char*       m_file;
    int               m_line;
    const char*       m_function;
    std::ostringstream m_stream;
};

}  // namespace snf

// ── Helper: resolve Logger from Application singleton ────────────────────────
// Placed outside the namespace so the macros remain as short identifiers.

namespace snf::detail {

inline Logger* globalLogger() noexcept
{
    if (Application* app = Application::instance()) {
        return &app->logger();
    }
    return nullptr;
}

}  // namespace snf::detail

// ── Public macros ─────────────────────────────────────────────────────────────

/**
 * @def sDebug()
 * @ingroup SNFCore_Logging
 * @brief Produces a `LogLevel::Debug` entry. Usage: `sDebug() << "msg" << val;`
 */
#define sDebug()    snf::LogEntryBuilder(snf::detail::globalLogger(), snf::LogLevel::Debug,    __FILE__, __LINE__, __func__)

/**
 * @def sInfo()
 * @ingroup SNFCore_Logging
 * @brief Produces a `LogLevel::Info` entry. Usage: `sInfo() << "msg";`
 */
#define sInfo()     snf::LogEntryBuilder(snf::detail::globalLogger(), snf::LogLevel::Info,     __FILE__, __LINE__, __func__)

/**
 * @def sWarning()
 * @ingroup SNFCore_Logging
 * @brief Produces a `LogLevel::Warning` entry.
 */
#define sWarning()  snf::LogEntryBuilder(snf::detail::globalLogger(), snf::LogLevel::Warning,  __FILE__, __LINE__, __func__)

/**
 * @def sError()
 * @ingroup SNFCore_Logging
 * @brief Produces a `LogLevel::Error` entry.
 */
#define sError()    snf::LogEntryBuilder(snf::detail::globalLogger(), snf::LogLevel::Error,    __FILE__, __LINE__, __func__)

/**
 * @def sCritical()
 * @ingroup SNFCore_Logging
 * @brief Produces a `LogLevel::Critical` entry.
 */
#define sCritical() snf::LogEntryBuilder(snf::detail::globalLogger(), snf::LogLevel::Critical, __FILE__, __LINE__, __func__)
