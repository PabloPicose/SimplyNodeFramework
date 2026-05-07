#include "SNFCore/ConsoleLogSink.h"

#include <SNFCore/LogLevel.h>
#include <SNFCore/LogMessage.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace snf {

namespace {

std::string formatTimestamp(std::chrono::system_clock::time_point tp)
{
    // Render as "YYYY-MM-DDTHH:MM:SS.mmm" (ISO-8601, milliseconds).
    const auto tt    = std::chrono::system_clock::to_time_t(tp);
    const auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                           tp.time_since_epoch()) % 1000;
    // Max realistic output is 23 chars ("YYYY-MM-DDTHH:MM:SS.mmm").
    // Use a generous buffer so the compiler's conservative range analysis
    // does not emit -Wformat-truncation warnings.
    char buf[64];
    std::tm  tm_val{};
#if defined(_WIN32)
    gmtime_s(&tm_val, &tt);
#else
    gmtime_r(&tt, &tm_val);
#endif
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                  tm_val.tm_year + 1900,
                  tm_val.tm_mon  + 1,
                  tm_val.tm_mday,
                  tm_val.tm_hour,
                  tm_val.tm_min,
                  tm_val.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

}  // anonymous namespace

void ConsoleLogSink::consume(const LogMessage& message)
{
    std::ostringstream oss;
    oss << std::this_thread::get_id();

    // One atomic write via a single fprintf to avoid interleaving.
    std::fprintf(stderr,
                 "%s [%-8s] (%s) %s:%d %s: %s\n",
                 formatTimestamp(message.timestamp).c_str(),
                 logLevelToString(message.level),
                 oss.str().c_str(),
                 message.file,
                 message.line,
                 message.function,
                 message.text.c_str());
}

}  // namespace snf
