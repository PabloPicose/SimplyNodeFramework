#include "SNFCore/TimeLapse.h"

#include <chrono>

namespace snf {

TimeLapse::TimeLapse() noexcept
{
    reset();
}

void TimeLapse::reset() noexcept
{
    const std::uint64_t now = nowNanoseconds();
    m_lastActivityNanoseconds.store(now, std::memory_order_relaxed);
    m_spanStartNanoseconds.store(now, std::memory_order_relaxed);
    m_lastDurationNanoseconds.store(0, std::memory_order_relaxed);
    m_running.store(false, std::memory_order_release);
}

void TimeLapse::touch() noexcept
{
    m_lastActivityNanoseconds.store(nowNanoseconds(), std::memory_order_release);
}

void TimeLapse::start() noexcept
{
    const std::uint64_t now = nowNanoseconds();
    m_spanStartNanoseconds.store(now, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
}

void TimeLapse::stop() noexcept
{
    const std::uint64_t now = nowNanoseconds();
    const std::uint64_t startedAt = m_spanStartNanoseconds.load(std::memory_order_acquire);

    if (m_running.exchange(false, std::memory_order_acq_rel)) {
        m_lastDurationNanoseconds.store(now > startedAt ? now - startedAt : 0, std::memory_order_release);
    }

    m_lastActivityNanoseconds.store(now, std::memory_order_release);
}

std::uint64_t TimeLapse::ageNanoseconds() const noexcept
{
    const std::uint64_t lastActivityNanoseconds = m_lastActivityNanoseconds.load(std::memory_order_acquire);
    if (lastActivityNanoseconds == 0) {
        return 0;
    }

    const std::uint64_t now = nowNanoseconds();
    return now > lastActivityNanoseconds ? now - lastActivityNanoseconds : 0;
}

std::uint64_t TimeLapse::lastDurationNanoseconds() const noexcept
{
    return m_lastDurationNanoseconds.load(std::memory_order_acquire);
}

bool TimeLapse::isRunning() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

std::uint64_t TimeLapse::nowNanoseconds() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace snf