#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <thread>
#include <list>
#include <memory>
#include <mutex>

namespace snf {

  class Node;
  class EventLoop;

class Application {
 public:
  Application(int argc, char** argv);

  ~Application();

  std::string getFullExecutablePath() const;

  static Application* instance();

  static std::thread::id threadId();

  void loopOnce();

  int run();

  void quit();

  std::size_t getRootNodesCount() const;

  Node* getRootNode(std::size_t index) const;

  /**
   * @brief Gets if the node pointer is still valid. This not checks if the node
   * is about to delete.
   * @param node The node pointer to check
   * @return True if the node memory is accessible, false otherwise
   */
  bool isNodeAlive(Node* node) const;

  /**
   * @brief Gets if the node pointer is marked to be deleted. If the node is
   * marked to be deleted OR the node is not memory accessible, this function
   * returns true.
   * @details It is important to check if the node is alive (memory accesible)
   * before calling this function.
   * @param node The node pointer to check
   * @return True if the node is marked to be deleted OR the node is not memory
   * accessible, false otherwise.
   */
  bool isNodeMarkedToDelete(Node* node) const;

  size_t getRootNodesToDeleteCount() const;

  size_t getAliveNodesCount() const;

  size_t getAliveNodesToDeleteCount() const;

  void setApplicationVersion(const std::string& version);

  std::list<std::string> getArguments() const;

  EventLoop* getOrCreateCurrentThreadEventLoop();

  EventLoop* getEventLoopByThreadId(std::thread::id threadId) const;

  size_t getEventLoopCount() const;

 private:
  void pushRootNodeDeleteLater(Node* node);

  //! This function must be called in the constructor of the Node class.
  //! Creates a key in the map with the node and the boolean value to true
  void registerAliveNode(Node* node);

  //! This function marks to false the boolean value of the node in the map
  //! to know that the node is marked to be deleted
  void markToDelete(Node* node);

  //! This function must be called in the destructor of the Node class
  void unregisterAliveNode(Node* node);

  EventLoop* mainEventLoop() const;

 private:
  friend class Node;
  static Application* m_instance;

  std::thread::id m_threadId;

  //! This map is used to keep the nodes alive. The boolean value is used to
  //! know if the node is marked to be deleted.
  std::unordered_map<Node*, bool> m_aliveNodes;
  mutable std::mutex m_aliveNodesMutex;

  std::unordered_map<std::thread::id, std::unique_ptr<EventLoop>> m_eventLoops;
  mutable std::mutex m_eventLoopsMutex;

  bool m_quit = false;

  std::string m_version;

  int m_argc = 0;

  char** m_argv = nullptr;
};
}
