#pragma once

/**
 * @file Application.h
 * @brief Application singleton: event loop lifecycle and node registry.
 * @ingroup SNFCore_Application
 */

#include <cstdint>
#include <functional>
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
class CommandLineParser;
class ThreadPool;

/**
 * @class Application
 * @ingroup SNFCore_Application
 * @brief Singleton that owns the main thread's EventLoop, tracks all live
 *        nodes, and drives the application run loop.
 *
 * Exactly one `Application` instance must exist for the lifetime of the
 * program. It registers every `Node` that is created and provides the
 * mechanism to check whether a raw `Node*` is still valid (used internally
 * by `NodePtr<T>`).
 *
 * Typical usage:
 * @code
 * int main(int argc, char** argv)
 * {
 *     snf::Application app(argc, argv);
 *     // ... create nodes, connect signals ...
 *     return app.run(); // blocks until quit()
 * }
 * @endcode
 */
class Application
{
public:
    /** @brief Callable type used by modules that provide their own main loop. */
    using RunLoopDriver = std::function<int()>;

    /**
     * @brief Constructs the Application singleton.
     * @param argc Argument count from `main()`.
     * @param argv Argument values from `main()`.
     */
    Application(int argc, char** argv);

    ~Application();

    /** @brief Returns the absolute path of the running executable. */
    std::string getFullExecutablePath() const;

    /**
     * @brief Returns the global Application singleton.
     * @return Pointer to the single Application instance, or `nullptr` if none
     *         has been constructed yet.
     */
    static Application* instance();

    /** @brief Returns the thread ID of the main thread (where Application was constructed). */
    static std::thread::id threadId();

    /**
     * @brief Runs one iteration of the main event loop without blocking.
     *
     * Processes all pending tasks, due timers, and I/O events once, then
     * returns. Useful for integrating the event loop into an external loop.
     */
    void loopOnce();

    /**
     * @brief Starts the application main loop.
     *
     * If a module such as SNFWidgets has registered a run-loop driver, this
     * delegates to that driver. Otherwise it runs the main SNFCore EventLoop
     * until `quit()` is called or the loop stops.
     *
     * @return Exit code from the registered driver, or 0 for the default loop.
     */
    int run();

    /**
     * @brief Requests all EventLoops to stop and unblocks `run()`.
     *
     * Safe to call from any thread or from within a signal handler.
     */
    void quit();

    /** @brief Returns the number of current root nodes. */
    std::size_t getRootNodesCount() const;

    /**
     * @brief Returns the root node at the given index.
     * @param index Zero-based index; must be less than `getRootNodesCount()`.
     */
    Node* getRootNode(std::size_t index) const;

    /**
     * @brief Gets if the node pointer is still valid. This not checks if the node
     * is about to delete.
     * @param node The node pointer to check.
     * @param generation The expected generation of the node.
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

    /** @brief Returns the number of root nodes pending deferred deletion. */
    size_t getRootNodesToDeleteCount() const;

    /** @brief Returns the total number of nodes currently registered (alive). */
    size_t getAliveNodesCount() const;

    /** @brief Returns the number of alive nodes that are marked for deletion. */
    size_t getAliveNodesToDeleteCount() const;

    /** @brief Sets a human-readable version string for the application. */
    void setApplicationVersion(const std::string& version);

    /**
     * @brief Returns a CommandLineParser for this Application.
     *
     * The returned parser is owned by the Application and lives until the
     * Application is destroyed. The parser can be used to register and parse
     * command-line options. This method returns the same parser instance on
     * each call (lazy-created on first invocation).
     *
     * @return Reference to the CommandLineParser instance.
     */
    CommandLineParser& getCommandLineParser();

    /** @brief Returns the command-line arguments passed to the constructor. */
    std::list<std::string> getArguments() const;

    /**
     * @brief Returns the EventLoop for the calling thread, creating one if
     *        it does not exist yet.
     *
     * The returned EventLoop is owned by the Application and lives until the
     * Application is destroyed.
     */
    EventLoop* getOrCreateCurrentThreadEventLoop();

    /**
     * @brief Returns the EventLoop associated with the given thread ID, or
     *        `nullptr` if no EventLoop has been created for that thread.
     */
    EventLoop* getEventLoopByThreadId(std::thread::id threadId) const;

    /** @brief Returns the total number of EventLoops currently managed. */
    size_t getEventLoopCount() const;

    /**
     * @brief Check if all EventLoops have no pending work (tasks, deletes, timers).
     * @return True if all EventLoops are idle, false otherwise.
     */
    bool allEventLoopsIdle() const;

    /** @brief Returns the Application-owned global ThreadPool. */
    ThreadPool* threadPool() const;

    /**
     * @brief Registers a module-owned main-loop driver used by `run()`.
     *
     * Framework modules such as SNFWidgets use this hook when the platform
     * event loop is not the plain SNFCore EventLoop. End-user code should
     * still call `Application::run()`, Qt-style, after constructing the
     * application objects.
     *
     * @param owner Stable pointer that identifies the registering object.
     * @param driver Main-loop callable invoked by `Application::run()`.
     */
    void setRunLoopDriver(void* owner, RunLoopDriver driver);

    /**
     * @brief Clears the registered main-loop driver if @p owner registered it.
     */
    void clearRunLoopDriver(void* owner);

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

    std::unique_ptr<CommandLineParser> m_commandLineParser;
    std::unique_ptr<ThreadPool> m_threadPool;
    RunLoopDriver m_runLoopDriver;
    void* m_runLoopDriverOwner = nullptr;
    mutable std::mutex m_runLoopDriverMutex;

    int m_argc = 0;

    char** m_argv = nullptr;
};
}  // namespace snf
