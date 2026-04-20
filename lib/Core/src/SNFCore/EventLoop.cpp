#include "EventLoop.h"

#include <SNFCore/Node.h>
#include <SNFCore/NodePtr.h>

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
    if (m_tasks.empty() && m_nodesToDelete.empty() && m_rootNodes.empty()) {
      break;
    }
    // If there are roots but no pending tasks/deletes, wait for something
    // to be posted before running the root tick again.
    if (m_rootNodes.empty()) {
      break;
    }
    m_cv.wait(lock, [this] {
      return !m_tasks.empty() || !m_nodesToDelete.empty() || m_stop.load();
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

} // namespace snf
