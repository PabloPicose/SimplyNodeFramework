#pragma once

/**
 * @file Node.h
 * @brief Base class for all objects in the node ownership tree.
 * @ingroup SNFCore_Nodes
 */

#include <SNFCore/NodeLifetimeBlock.h>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace snf {

class EventLoop;
class ThreadPool;

/**
 * @class Node
 * @ingroup SNFCore_Nodes
 * @brief Abstract base class for all framework-managed objects.
 *
 * Every `Node` participates in a parent-child ownership tree. When a parent
 * is destroyed it recursively destroys its children. A node without a parent
 * is called a *root node* and is tracked by the `Application`.
 *
 * Nodes must always be heap-allocated (via `new`). Stack allocation is
 * supported only for leaf nodes that are never passed to `addChild()` and
 * never call `deleteLater()`.
 *
 * Each node is bound to the thread on which it was constructed. Use
 * `ownerEventLoop()` to post work back to that thread.
 *
 * Subclasses must implement `update()`, which is called once per event-loop
 * iteration by `run()`.
 */
class Node
{
public:
    virtual ~Node();

    /** @brief Sets a human-readable name for debugging purposes. */
    void setName(const std::string& name);

    /** @brief Returns the node's name, or an empty string if none was set. */
    std::string getName() const;

    /**
     * @brief Adds a child to the node and will take ownership of the child.
     * @details If the child is a root node it will be removed from the root nodes
     * list.
     * @note If the child had been previously created as an object the use of
     * deleteLater in the child is not allowed and the program will crash. For
     * example, creating a Node myChild; and then add this child to another node
     * will crash in the moment of deleteLater moment.
     * @param child A valid pointer. The child must not be nullptr.
     */
    void addChild(Node* child);

    /**
     * @brief Returns the child at the given zero-based index.
     * @param index Must be less than `childrenCount()`.
     */
    Node* getChild(std::size_t index) const;

    /** @brief Returns `true` if @p child is a direct child of this node. */
    bool isChild(Node* child) const;

    /**
     * @brief Schedules this node for deletion on the next event-loop
     *        iteration.
     *
     * Safe to call from within `update()`, signal handlers, or any context
     * where immediate `delete` would be unsafe. The node's `NodePtr<T>`
     * references become expired immediately after this call.
     *
     * @note Do not call `deleteLater()` on stack-allocated nodes.
     */
    void deleteLater();

    /**
     * @brief Calls `update()` once.
     *
     * Normally called by the `Application` each event-loop iteration.
     * Can also be called manually to trigger an immediate update outside
     * the loop.
     */
    void run();

    /**
     * @brief Reparents this node to @p parent.
     *
     * If the node was previously a root node it is removed from the
     * `Application`'s root list. The new parent takes ownership.
     */
    void setParent(Node* parent);

    /** @brief Returns the parent node, or `nullptr` if this is a root node. */
    Node* parent() const;

    /** @brief Returns the number of direct children. */
    std::size_t childrenCount() const;

    /** @brief Returns `true` if this node has no parent. */
    bool isRoot() const;

    /** @brief Returns the number of children currently pending deferred deletion. */
    size_t childrenToDeleteCount() const;

    /** @brief Returns the thread ID of the thread that constructed this node. */
    std::thread::id ownerThreadId() const;

    /** @brief Returns the thread ID of the thread this node currently lives on. */
    std::thread::id threadId() const;

    /**
     * @brief Returns the EventLoop of the owner thread.
     *
     * Returns `nullptr` if the owner thread has not yet called
     * `Application::getOrCreateCurrentThreadEventLoop()`.
     */
    EventLoop* ownerEventLoop() const;

    /**
     * @brief Migrates this node and all its descendants to @p targetThreadId.
     *
     * If called from the owner thread the migration is applied immediately.
     * If called from any other thread the migration is posted to the owner
     * EventLoop and executed asynchronously; the method returns `true` to
     * indicate that the request was accepted.
     *
     * Preconditions (checked at call time; return `false` on failure):
     * - The current node must not have a parent whose ownerThreadId differs
     *   from the target (i.e. only whole subtrees or root nodes may migrate).
     * - The target thread must already have an EventLoop registered with the
     *   Application (call `Application::getOrCreateCurrentThreadEventLoop()`
     *   from that thread before migrating to it).
     *
     * @param targetThreadId Thread ID to migrate to.
     * @return `true` if the migration was applied or successfully enqueued;
     *         `false` if the preconditions were not met.
     */
    bool moveToThread(std::thread::id targetThreadId);

    /**
     * @brief Migrates this node and its descendants to any worker in @p pool.
     *
     * If @p pool is null, the Application-owned global ThreadPool is used.
     * Throws if the Application-owned global ThreadPool does not exist.
     * Returns false when no worker EventLoop is available.
     */
    bool moveToThreadPool(ThreadPool* pool = nullptr);

    /**
     * @brief Returns the generation counter used by the Application registry.
     *
     * The counter is unique per node allocation and is incremented when the
     * node is registered with the `Application`.
     */
    std::uint64_t generation() const;

    /**
     * @brief Returns the lifetime control block shared with all `NodePtr<T>`
     *        copies of this node.
     *
     * The block is heap-allocated separately from the node and survives
     * node destruction as long as at least one `NodePtr<T>` exists.
     * All liveness checks in `NodePtr` use this block atomically,
     * without taking any global lock.
     */
    std::shared_ptr<NodeLifetimeBlock> lifetimeBlock() const;

protected:
    /**
     * @brief Constructs a Node, optionally attaching it to @p parent.
     * @param parent If non-null, the new node becomes a child of @p parent.
     *               If null, the node is registered as a root node with the
     *               Application.
     */
    explicit Node(Node* parent = nullptr);

    /**
     * @brief Called once per event-loop iteration by `run()`.
     *
     * Override to implement per-frame logic. The implementation must not
     * block; long-running work should be posted to the event loop or run on
     * a separate thread.
     */
    virtual void update();

    /**
     * @brief Called on the owner thread just before the node is migrated to
     *        a new EventLoop.
     *
     * Override in subclasses that hold EventLoop resources (e.g. IOEvent)
     * to release those resources from the current loop before the affinity
     * fields are updated. The default implementation is a no-op.
     *
     * @param newLoop The EventLoop the node is about to be moved to.
     */
    virtual void onAboutToMoveToThread(EventLoop* newLoop);

    /**
     * @brief Called on the owner thread immediately after the node's affinity
     *        fields have been updated to the new EventLoop.
     *
     * Override in subclasses to re-acquire EventLoop resources on the new
     * loop. The default implementation is a no-op.
     *
     * @param oldLoop The EventLoop the node was migrated away from.
     */
    virtual void onMovedToThread(EventLoop* oldLoop);

private:
    void pushDeleteChild(Node* child);

    // Performs the actual recursive migration; must be called on owner thread.
    void applyMoveToThread(EventLoop* targetLoop);

private:
    Node* m_parent = nullptr;
    std::vector<Node*> m_children;
    std::vector<Node*> m_childrenToDelete;
    bool m_isRoot = false;
    std::string m_name;
    std::thread::id m_ownerThreadId;
    EventLoop* m_ownerEventLoop = nullptr;
    std::uint64_t m_generation = 0;
    std::shared_ptr<NodeLifetimeBlock> m_lifetimeBlock;
};

}  // namespace snf
