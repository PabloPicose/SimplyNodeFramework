#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace snf {
class Node;
class Timer;

class EventLoop
{
public:
    using Task = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void post(Task t);
    void enqueueDelete(Node* node);
    void addNode(Node* node);
    void removeNode(Node* node);

    void addRootNode(Node* node);
    void removeRootNode(Node* node);
    std::size_t getRootNodesCount() const;
    Node* getRootNode(std::size_t index) const;
    std::size_t getRootNodesToDeleteCount() const;

    void run();
    void stop();

    void scheduleTimer(Timer* timer, std::chrono::steady_clock::time_point deadline, std::uint64_t generation);
    void cancelTimer(Timer* timer);

    bool isInThisThread() const noexcept;

    std::size_t pendingDeleteCount() const;
    std::size_t registeredNodesCount() const;
    std::size_t activeTimerCount() const;
    bool hasPendingWork() const;

private:
    bool hasPendingWorkLocked() const;
    bool popNextTask(Task& task);
    std::vector<Node*> takePendingDeletes();

    struct TimerEntry {
        Timer* timer = nullptr;
        std::chrono::steady_clock::time_point deadline{};
        std::uint64_t generation = 0;
    };

    bool hasDueTimerLocked(std::chrono::steady_clock::time_point now) const;
    bool nextTimerDeadlineLocked(std::chrono::steady_clock::time_point& deadline) const;
    std::vector<TimerEntry> takeDueTimers(std::chrono::steady_clock::time_point now);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<Task> m_tasks;
    std::vector<Node*> m_nodesToDelete;
    std::vector<Node*> m_nodes;
    std::vector<Node*> m_rootNodes;
    std::vector<TimerEntry> m_timers;
    std::atomic_bool m_stop{false};
    const std::thread::id m_owner;
};
}  // namespace snf
