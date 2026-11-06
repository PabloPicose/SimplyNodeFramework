#include "SNFCore/Timer.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/Executor.h"
#include "SNFCore/NodePtr.h"

namespace snf {

namespace {

Timer::Duration sanitizeInterval(Timer::Duration interval)
{
    if (interval < Timer::Duration::zero()) {
        return Timer::Duration::zero();
    }
    return interval;
}

}  // namespace

Timer::Timer(Node* parent) : Node(parent) {}

Timer::~Timer()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active = false;
        ++m_generation;
    }

    if (Executor* owner = ownerExecutor()) {
        owner->cancelTimer(this);
    }
}

void Timer::start()
{
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([timer = NodePtr<Timer>(this)]() {
            if (timer) {
                timer->start();
            }
        });
        return;
    }

    Duration nextInterval;
    std::uint64_t generation = 0;
    std::chrono::steady_clock::time_point deadline;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        nextInterval = sanitizeInterval(m_interval);
        m_interval = nextInterval;
        m_active = true;
        generation = ++m_generation;
        deadline = std::chrono::steady_clock::now() + nextInterval;
        m_deadline = deadline;
    }

    if (Executor* owner = ownerExecutor()) {
        owner->scheduleTimer(this, deadline, generation);
    }
}

void Timer::start(Duration interval)
{
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([timer = NodePtr<Timer>(this), interval]() {
            if (timer) {
                timer->start(interval);
            }
        });
        return;
    }

    const Duration nextInterval = sanitizeInterval(interval);
    std::uint64_t generation = 0;
    std::chrono::steady_clock::time_point deadline;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_interval = nextInterval;
        m_active = true;
        generation = ++m_generation;
        deadline = std::chrono::steady_clock::now() + nextInterval;
        m_deadline = deadline;
    }

    if (Executor* owner = ownerExecutor()) {
        owner->scheduleTimer(this, deadline, generation);
    }
}

void Timer::start(int intervalMs) { start(Duration(intervalMs)); }

void Timer::stop()
{
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([timer = NodePtr<Timer>(this)]() {
            if (timer) {
                timer->stop();
            }
        });
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active = false;
        ++m_generation;
    }

    if (Executor* owner = ownerExecutor()) {
        owner->cancelTimer(this);
    }
}

void Timer::setInterval(Duration interval)
{
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([timer = NodePtr<Timer>(this), interval]() {
            if (timer) {
                timer->setInterval(interval);
            }
        });
        return;
    }

    const Duration nextInterval = sanitizeInterval(interval);
    bool shouldReschedule = false;
    std::uint64_t generation = 0;
    std::chrono::steady_clock::time_point deadline;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_interval = nextInterval;
        if (m_active) {
            shouldReschedule = true;
            generation = ++m_generation;
            deadline = std::chrono::steady_clock::now() + nextInterval;
            m_deadline = deadline;
        }
    }

    if (shouldReschedule) {
        if (Executor* owner = ownerExecutor()) {
            owner->scheduleTimer(this, deadline, generation);
        }
    }
}

void Timer::setInterval(int intervalMs) { setInterval(Duration(intervalMs)); }

Timer::Duration Timer::interval() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_interval;
}

void Timer::setSingleShot(bool singleShot)
{
    if (Executor* owner = ownerExecutor(); owner && ! owner->isCurrent()) {
        owner->post([timer = NodePtr<Timer>(this), singleShot]() {
            if (timer) {
                timer->setSingleShot(singleShot);
            }
        });
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_singleShot = singleShot;
}

bool Timer::isSingleShot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_singleShot;
}

bool Timer::isActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active;
}

void Timer::update() {}

void Timer::onAboutToMoveToThread(EventLoop* /*newLoop*/)
{
    // The pending timeout lives with the old owner. Remove it so the old owner
    // neither emits timeout nor keeps a pointer to this timer.
    if (Executor* owner = ownerExecutor()) {
        owner->cancelTimer(this);
    }
}

void Timer::onMovedToThread(EventLoop* /*oldLoop*/)
{
    // ownerExecutor() already is the new owner. Re-arm with the same deadline
    // and generation so the remaining time is preserved.
    bool active = false;
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        active = m_active;
        deadline = m_deadline;
        generation = m_generation;
    }

    if (active) {
        if (Executor* owner = ownerExecutor()) {
            owner->scheduleTimer(this, deadline, generation);
        }
    }
}

void Timer::dispatchTimeout(std::uint64_t generation)
{
    bool shouldEmit = false;
    bool shouldReschedule = false;
    Duration nextInterval = Duration::zero();
    std::uint64_t nextGeneration = 0;
    std::chrono::steady_clock::time_point nextDeadline;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (! m_active || generation != m_generation) {
            return;
        }

        shouldEmit = true;
        if (m_singleShot) {
            m_active = false;
            ++m_generation;
        } else {
            nextInterval = sanitizeInterval(m_interval);
            shouldReschedule = true;
            nextGeneration = ++m_generation;
            nextDeadline = std::chrono::steady_clock::now() + nextInterval;
            m_deadline = nextDeadline;
        }
    }

    if (shouldReschedule) {
        if (Executor* owner = ownerExecutor()) {
            owner->scheduleTimer(this, nextDeadline, nextGeneration);
        }
    }

    if (shouldEmit) {
        timeout.emit();
    }
}

}  // namespace snf
