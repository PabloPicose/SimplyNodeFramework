#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace snf {

class Node;
class EventLoop;

class Application
{
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
    bool isNodeAlive(Node* node, std::uint64_t generation) const;

    /**
     * @brief Gets if the node pointer is marked to be deleted. If the node is
     * marked to be deleted OR the node is not memory accessible, this function
     * returns true.
     * @details It is important to check if the node is alive (memory accesible)
     * before calling this function.
     * @param node The node pointer to check
     * @param generation The generation of the node at the time NodePtr was created
     * @return True if the node is marked to be deleted OR the node is not memory
     * accessible, false otherwise.
     */
    bool isNodeMarkedToDelete(Node* node, std::uint64_t generation) const;

    size_t getRootNodesToDeleteCount() const;

    size_t getAliveNodesCount() const;

    size_t getAliveNodesToDeleteCount() const;

    void setApplicationVersion(const std::string& version);

    std::list<std::string> getArguments() const;

    EventLoop* getOrCreateCurrentThreadEventLoop();

    EventLoop* getEventLoopByThreadId(std::thread::id threadId) const;

    size_t getEventLoopCount() const;

    /**
     * @brief Check if all EventLoops have no pending work (tasks, deletes, timers).
     * @return True if all EventLoops are idle, false otherwise.
     */
    bool allEventLoopsIdle() const;

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

    struct NodeEntry {
        std::uint64_t generation = 0;
        bool markedForDelete = false;
    };

    //! This map is used to keep the nodes alive. The NodeEntry stores the
    //! generation ID and whether the node is marked for deletion.
    std::unordered_map<Node*, NodeEntry> m_aliveNodes;
    mutable std::mutex m_aliveNodesMutex;

    std::unordered_map<std::thread::id, std::unique_ptr<EventLoop>> m_eventLoops;
    mutable std::mutex m_eventLoopsMutex;

    bool m_quit = false;

    std::string m_version;

    int m_argc = 0;

    char** m_argv = nullptr;
};
}  // namespace snf
