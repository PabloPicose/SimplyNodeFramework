#pragma once

/**
 * @file Executor.h
 * @brief Interface of whatever runs a node's work: the node's owner.
 * @ingroup SNFCore
 */

#include <chrono>
#include <cstdint>
#include <functional>

namespace snf {

class EventLoop;
class Node;
class Timer;

/**
 * @class Executor
 * @ingroup SNFCore
 * @brief Runs the work of the nodes it owns, one piece at a time.
 *
 * Every node has an owner executor. Queued and `Auto` signal deliveries,
 * `Node::deleteLater()` and asynchronous migrations go through it, so a
 * node's slots, deferred deletion and migration hooks always run inside its
 * owner. `EventLoop` runs everything on one fixed thread; the strand behind a
 * `PooledNode` runs on whichever `TaskPool` worker is free, one task at a
 * time.
 *
 * Contract for implementations:
 * - `post()` is thread-safe and never runs @p task inline.
 * - Work posted to one executor runs in order and never concurrently with
 *   other work of the same executor.
 * - While `isCurrent()` is true the caller is running inside the executor,
 *   and the executor's nodes cannot change owner under it (a node only
 *   changes owner from inside its owner).
 */
class Executor
{
public:
    /** @brief Callable type for posted work. */
    using Task = std::function<void()>;

    virtual ~Executor() = default;

    /** @brief Queues @p task to run inside this executor. Thread-safe. */
    virtual void post(Task task) = 0;

    /** @brief Returns `true` if the calling thread is running inside this executor. */
    virtual bool isCurrent() const noexcept = 0;

    /**
     * @brief Returns `true` while the executor may be running its nodes' work.
     *
     * Used to detect node state touched from outside the owner while the
     * owner may be touching it too.
     */
    virtual bool isActive() const noexcept = 0;

    /** @brief Returns `true` once the executor will never run again. */
    virtual bool isRetired() const noexcept = 0;

    /**
     * @brief Schedules the deferred deletion of @p node.
     *
     * Must be called from inside the executor (`isCurrent()`); the node is
     * destroyed later, also inside it.
     */
    virtual void enqueueDelete(Node* node) = 0;

    /** @name Node bookkeeping
     *  Called by Node when one of the executor's nodes is created, destroyed,
     *  migrated or re-parented. A root node is one without a parent; the
     *  executor destroys the roots it still owns when it is torn down.
     *  @{ */
    virtual void addNode(Node* node) = 0;
    virtual void removeNode(Node* node) = 0;
    virtual void addRootNode(Node* node) = 0;
    virtual void removeRootNode(Node* node) = 0;
    /** @} */

    /** @name Timers
     *  Called by Timer, from inside the executor. When @p deadline is reached
     *  the executor calls the timer's timeout dispatch inside itself with
     *  @p generation; the timer ignores stale generations. Scheduling a timer
     *  again replaces its previous deadline.
     *  @{ */
    virtual void scheduleTimer(Timer* timer, std::chrono::steady_clock::time_point deadline, std::uint64_t generation)
        = 0;
    virtual void cancelTimer(Timer* timer) = 0;
    /** @} */

    /** @name File descriptors
     *  Called by IOEvent, from inside the executor. While @p fd is ready for
     *  one of @p events, @p callback runs inside the executor with the native
     *  epoll mask; a callback is never queued twice for the same descriptor.
     *  @{ */
    virtual void registerIO(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback) = 0;
    virtual void modifyIO(int fd, std::uint32_t events) = 0;
    virtual void unregisterIO(int fd) = 0;
    virtual bool hasIOWatch(int fd) const = 0;
    /** @} */

    /** @brief Returns this executor as an EventLoop, or `nullptr` if it is not one. */
    virtual EventLoop* eventLoop() noexcept { return nullptr; }

    /** @copydoc eventLoop() */
    virtual const EventLoop* eventLoop() const noexcept { return nullptr; }

protected:
    Executor() = default;
    Executor(const Executor&) = default;
    Executor& operator=(const Executor&) = default;
};

}  // namespace snf
