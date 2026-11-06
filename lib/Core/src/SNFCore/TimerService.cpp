#include "SNFCore/TimerService.h"

#include "SNFCore/Strand.h"

#include <utility>
#include <vector>

namespace snf::detail {

TimerService::~TimerService() { stop(); }

void TimerService::schedule(Strand* strand, Timer* timer, Clock::time_point deadline, std::uint64_t generation)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) {
            return;
        }
        if (auto found = m_index.find(timer); found != m_index.end()) {
            m_queue.erase(found->second);
            m_index.erase(found);
        }
        m_index[timer] = m_queue.emplace(deadline, Entry{strand, NodePtr<Timer>(timer), generation});
        if (! m_thread.joinable()) {
            m_thread = std::thread([this]() { run(); });
        }
    }
    m_changed.notify_one();
}

void TimerService::cancel(Timer* timer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto found = m_index.find(timer); found != m_index.end()) {
        m_queue.erase(found->second);
        m_index.erase(found);
    }
}

void TimerService::stop()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_queue.clear();
        m_index.clear();
        thread.swap(m_thread);
    }
    m_changed.notify_all();
    if (thread.joinable()) {
        thread.join();
    }
}

std::size_t TimerService::armedCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void TimerService::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (! m_stopped) {
        if (m_queue.empty()) {
            m_changed.wait(lock);
            continue;
        }
        const Clock::time_point next = m_queue.begin()->first;
        if (Clock::now() < next) {
            m_changed.wait_until(lock, next);
            continue;
        }

        // Take every due timer out, then post outside the lock (post() takes
        // the strand's own lock).
        std::vector<Entry> due;
        const Clock::time_point now = Clock::now();
        while (! m_queue.empty() && m_queue.begin()->first <= now) {
            auto first = m_queue.begin();
            m_index.erase(first->second.timer.get());
            due.push_back(std::move(first->second));
            m_queue.erase(first);
        }
        lock.unlock();
        for (Entry& entry : due) {
            Strand* strand = entry.strand;
            strand->post([strand, timer = std::move(entry.timer), generation = entry.generation]() {
                // Inside the strand: drop it if the timer died or moved away
                // since the timeout was posted.
                if (timer && timer.ownerExecutor() == strand) {
                    timer->dispatchTimeout(generation);
                }
            });
        }
        lock.lock();
    }
}

}  // namespace snf::detail
