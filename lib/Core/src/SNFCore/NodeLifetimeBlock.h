#pragma once

/**
 * @file NodeLifetimeBlock.h
 * @brief External control block for lock-free Node liveness tracking.
 * @ingroup SNFCore_Nodes
 */

#include <atomic>

namespace snf {

class Executor;

/**
 * @struct NodeLifetimeBlock
 * @ingroup SNFCore_Nodes
 * @brief Small heap-allocated control block that outlives its owner Node.
 *
 * Each Node owns a `std::shared_ptr<NodeLifetimeBlock>`. Every `NodePtr<T>`
 * copy holds a second `shared_ptr` to the same block, keeping it alive even
 * after the Node object itself has been destroyed.
 *
 * All fields are `std::atomic`, so `NodePtr` liveness checks are completely
 * lock-free and never touch the Application's global registry.
 *
 * Lifecycle:
 * - Created in `Node` constructor. Both fields start at their default values
 *   (`alive = true`, `markedForDelete = false`).
 * - `markedForDelete` is set to `true` by `Node::deleteLater()` immediately,
 *   before any thread routing or EventLoop queuing occurs.
 * - `alive` is set to `false` at the very beginning of `Node::~Node()`, so
 *   any concurrent `NodePtr` check sees the node as dead as soon as
 *   destruction starts.
 * - `owner` is the node's current owner Executor (today always an
 *   EventLoop). It is written only from inside the owner (construction and
 *   migration) and may be read from any thread, even after the node has been
 *   destroyed. A reader running inside the executor it finds there is inside
 *   the owner, and the value cannot change until the owner lets it.
 */
struct NodeLifetimeBlock {
    std::atomic<bool> alive{true};
    std::atomic<bool> markedForDelete{false};
    std::atomic<Executor*> owner{nullptr};
};

}  // namespace snf
