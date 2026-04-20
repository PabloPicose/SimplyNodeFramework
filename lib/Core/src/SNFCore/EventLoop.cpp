#include "EventLoop.h"

#include <SNFCore/Node.h>
#include <SNFCore/NodePtr.h>
#include <SNFCore/Timer.h>

#include <algorithm>


namespace snf {

EventLoop::EventLoop() : m_owner(std::this_thread::get_id()) {}

EventLoop::~EventLoop() {
  // Destroy root nodes owned by this loop. Iterate a copy because
  // ~Node() calls removeRootNode(), which mutates m_rootNodes.
  const std::vector<Node*> roots = [this] {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rootNodes;
  }();
  for (Node* node : roots) {
    delete node;
  }
}

bool EventLoop::isInThisThread() const noexcept {
  return std::this_thread::get_id() == m_owner;
}

void EventLoop::enqueueDelete(Node* node) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodesToDelete.push_back(node);
  }
  m_cv.notify_one();
}

void EventLoop::addNode(Node* node) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_nodes.push_back(node);
}

void EventLoop::removeNode(Node* node) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_nodes.erase(std::remove(m_nodes.begin(), m_nodes.end(), node), m_nodes.end());
}

void EventLoop::addRootNode(Node* node) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rootNodes.push_back(node);
  }
  m_cv.notify_one();
}

void EventLoop::removeRootNode(Node* node) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_rootNodes.erase(std::remove(m_rootNodes.begin(), m_rootNodes.end(), node),
                    m_rootNodes.end());
}

std::size_t EventLoop::getRootNodesCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_rootNodes.size();
}

Node* EventLoop::getRootNode(std::size_t index) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (index >= m_rootNodes.size()) {
    return nullptr;
  }
  return m_rootNodes[index];
}

std::size_t EventLoop::getRootNodesToDeleteCount() const {
  return pendingDeleteCount();
}

void EventLoop::run() {
  while (!m_stop.load()) {
    // Drain task queue.
    Task task;
    while (popNextTask(task)) {
      task();
    }

    // Drain deferred deletes.
    for (Node* node : takePendingDeletes()) {
      NodePtr nodePtr(node);
      if (nodePtr) {
        delete node;
      }
    }

    for (TimerEntry& timerEntry :
         takeDueTimers(std::chrono::steady_clock::now())) {
      if (timerEntry.timer) {
        timerEntry.timer->dispatchTimeout(timerEntry.generation);
      }
    }

    // Tick all root nodes.
    std::vector<Node*> roots;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      roots = m_rootNodes;
    }
    for (Node* node : roots) {
      node->run();
    }

    // Block until there is work or a stop request.
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_stop.load()) {
      break;
    }
    if (m_tasks.empty() && m_nodesToDelete.empty() && m_rootNodes.empty() &&
        m_timers.empty()) {
      break;
    }

    std::chrono::steady_clock::time_point nextDeadline;
    if (nextTimerDeadlineLocked(nextDeadline)) {
      m_cv.wait_until(lock, nextDeadline, [this] {
        return !m_tasks.empty() || !m_nodesToDelete.empty() ||
               hasDueTimerLocked(std::chrono::steady_clock::now()) ||
               m_stop.load();
      });
      continue;
    }

    m_cv.wait(lock, [this] {
      return !m_tasks.empty() || !m_nodesToDelete.empty() ||
             hasDueTimerLocked(std::chrono::steady_clock::now()) ||
             m_stop.load();
    });
  }
}

void EventLoop::stop() {
  m_stop.store(true);
  m_cv.notify_all();
}

void EventLoop::post(EventLoop::Task t) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.push(std::move(t));
  }
  m_cv.notify_one();
}

std::size_t EventLoop::pendingDeleteCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_nodesToDelete.size();
}

std::size_t EventLoop::registeredNodesCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_nodes.size();
}

std::size_t EventLoop::activeTimerCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_timers.size();
}

void EventLoop::scheduleTimer(Timer* timer,
                              std::chrono::steady_clock::time_point deadline,
                              std::uint64_t generation) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.erase(std::remove_if(m_timers.begin(),
                                  m_timers.end(),
                                  [timer](const TimerEntry& entry) {
                                    return entry.timer == timer;
                                  }),
                   m_timers.end());
    m_timers.push_back(TimerEntry{timer, deadline, generation});
  }
  m_cv.notify_one();
}

void EventLoop::cancelTimer(Timer* timer) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.erase(std::remove_if(m_timers.begin(),
                                  m_timers.end(),
                                  [timer](const TimerEntry& entry) {
                                    return entry.timer == timer;
                                  }),
                   m_timers.end());
  }
  m_cv.notify_one();
}

bool EventLoop::hasPendingWork() const {
  // Caller must not hold m_mutex.
  std::lock_guard<std::mutex> lock(m_mutex);
  return !m_tasks.empty() || !m_nodesToDelete.empty();
}

bool EventLoop::popNextTask(Task& task) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_tasks.empty()) {
    return false;
  }

  task = std::move(m_tasks.front());
  m_tasks.pop();
  return true;
}

std::vector<Node*> EventLoop::takePendingDeletes() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<Node*> nodesToDelete;
  nodesToDelete.swap(m_nodesToDelete);
  return nodesToDelete;
}

bool EventLoop::hasDueTimerLocked(
    std::chrono::steady_clock::time_point now) const {
  return std::any_of(m_timers.begin(), m_timers.end(), [now](const TimerEntry& entry) {
    return entry.deadline <= now;
  });
}

bool EventLoop::nextTimerDeadlineLocked(
    std::chrono::steady_clock::time_point& deadline) const {
  if (m_timers.empty()) {
    return false;
  }

  const auto it = std::min_element(
      m_timers.begin(),
      m_timers.end(),
      [](const TimerEntry& lhs, const TimerEntry& rhs) {
        return lhs.deadline < rhs.deadline;
      });
  deadline = it->deadline;
  return true;
}

std::vector<EventLoop::TimerEntry> EventLoop::takeDueTimers(
    std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<TimerEntry> dueTimers;
  auto it = m_timers.begin();
  while (it != m_timers.end()) {
    if (it->deadline <= now) {
      dueTimers.push_back(*it);
      it = m_timers.erase(it);
      continue;
    }
    ++it;
  }
  return dueTimers;
}

} // namespace snf
