#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <vector>


namespace snf {
class Node;

class EventLoop {
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

  bool isInThisThread() const noexcept;

  std::size_t pendingDeleteCount() const;
  std::size_t registeredNodesCount() const;

private:
  bool hasPendingWork() const;
  bool popNextTask(Task& task);
  std::vector<Node*> takePendingDeletes();

private:
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::queue<Task> m_tasks;
  std::vector<Node*> m_nodesToDelete;
  std::vector<Node*> m_nodes;
  std::vector<Node*> m_rootNodes;
  std::atomic_bool m_stop{false};
  const std::thread::id m_owner;
};
} // namespace snf
