#pragma once

/**
 * @file TimeLapse.h
 * @brief Small reusable helper for measuring elapsed time and cycle duration.
 * @ingroup SNFCore
 */

#include <atomic>
#include <cstdint>

namespace snf {

/**
 * @class TimeLapse
 * @ingroup SNFCore
 * @brief Lightweight timing helper for reusable elapsed-time measurements.
 *
 * `TimeLapse` is intentionally small and allocation-free. It is useful for
 * tracking both "time since last activity" and "duration of the last span"
 * without baking that logic into a specific subsystem such as `EventLoop`.
 */
class TimeLapse
{
public:
    TimeLapse() noexcept;

    /** @brief Resets the tracker to "just updated" and clears the last duration. */
    void reset() noexcept;

    /** @brief Records that useful work just happened. */
    void touch() noexcept;

    /** @brief Starts a measured span. Call `stop()` to record its duration. */
    void start() noexcept;

    /** @brief Stops the active span and stores its duration. */
    void stop() noexcept;

    /** @brief Returns the age in nanoseconds since the last `touch()` or `stop()`. */
    std::uint64_t ageNanoseconds() const noexcept;

    /** @brief Returns the duration in nanoseconds of the last completed span. */
    std::uint64_t lastDurationNanoseconds() const noexcept;

    /** @brief Returns `true` while a measured span is active. */
    bool isRunning() const noexcept;

private:
    static std::uint64_t nowNanoseconds() noexcept;

    std::atomic<std::uint64_t> m_lastActivityNanoseconds{0};
    std::atomic<std::uint64_t> m_spanStartNanoseconds{0};
    std::atomic<std::uint64_t> m_lastDurationNanoseconds{0};
    std::atomic_bool m_running{false};
};

}  // namespace snf