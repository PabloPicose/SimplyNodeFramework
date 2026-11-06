#pragma once

/**
 * @file PooledNode.h
 * @brief Node that can run on any free TaskPool worker.
 * @ingroup SNFCore_Nodes
 */

#include <SNFCore/Node.h>

namespace snf {

class TaskPool;

/**
 * @class PooledNode
 * @ingroup SNFCore_Nodes
 * @brief A node written to run on a TaskPool, on whichever worker is free.
 *
 * After `moveToTaskPool()` the node and its subtree are owned by a strand of
 * the pool: its slots (queued and `Auto` deliveries), deferred deletes and
 * migration hooks run on any free worker, but never two at a time and always
 * in the order they were queued. Many pooled nodes therefore spread over all
 * the workers by themselves, while each one is written without locks, as if
 * it had its own thread.
 *
 * Deriving from PooledNode states that the class meets the pool contract:
 * - The thread changes from one slot call to the next: do not rely on
 *   `std::this_thread::get_id()`, `thread_local` or `ThreadStorage` (for
 *   example a per-thread database connection) across calls.
 * - There is no event-loop tick: `update()` is final and never called.
 * - Timers work: a Timer in the subtree fires inside the node's strand (a
 *   per-pool deadline thread posts each timeout to it), keeps its remaining
 *   time when the node moves in or out of the pool, and is disarmed when the
 *   pool shuts down.
 * - Sockets work (TcpSocket, TcpServer, UdpSocket, LocalSocket,
 *   LocalServer): a per-pool I/O thread watches their descriptors and runs
 *   their callbacks inside the node's strand. A descriptor stays quiet while
 *   its callback is queued or running, and is watched again right after.
 * - No Process in the subtree, nor anything else whose `requiresEventLoop()`
 *   is true: moveToTaskPool() rejects such a subtree, and creating one as a
 *   child inside a slot throws.
 * - Nodes created inside a slot belong to the node's strand: children of it,
 *   or new roots. A new PooledNode root can then call moveToTaskPool() to get
 *   a strand of its own (e.g. one per accepted connection).
 * - A slot must not wait for other work of the same pool.
 *
 * Signals emitted inside the node to nodes of the same subtree are delivered
 * synchronously (`Auto`); to other nodes they are queued to the receiver.
 *
 * @code
 * class Analyzer : public snf::PooledNode {
 * public:
 *     void onSample(double value);        // no locks needed
 * };
 *
 * auto* analyzer = new Analyzer();
 * analyzer->moveToTaskPool();             // the Application's TaskPool
 * // sampleReady: a snf::Signal<double> of any node, on any thread
 * source->sampleReady.connect(snf::NodePtr<Analyzer>(analyzer), &Analyzer::onSample);
 * @endcode
 */
class PooledNode : public Node
{
public:
    explicit PooledNode(Node* parent = nullptr);
    ~PooledNode() override;

    /**
     * @brief Moves this node and its subtree to a strand of @p pool.
     *
     * If @p pool is null the Application's TaskPool is used. Called from
     * outside the node's owner, the move is applied later inside the owner
     * and `true` means the request was accepted.
     *
     * The node can return to a fixed thread with `moveToThread()` (or
     * `moveToThreadPool()`).
     *
     * @return `false` if the node has a parent (only whole subtrees move),
     *         the subtree contains a node that requiresEventLoop(), or the
     *         pool has shut down. `true` if moved, accepted, or already on a
     *         strand of @p pool of its own. A node created inside another
     *         pooled node's slot starts on that node's strand; calling this
     *         from there gives it a strand of its own. When called from outside the owner only the
     *         pool check is done before accepting; the others are done when
     *         the move is applied (a rejected move leaves the node where it
     *         is). @p pool must outlive an accepted request.
     * @throws std::runtime_error if @p pool is null and there is no
     *         Application TaskPool.
     */
    bool moveToTaskPool(TaskPool* pool = nullptr);

protected:
    /** @brief Never called: a pooled node has no event-loop tick. */
    void update() final;

private:
    // Inside the current owner: checks the preconditions and moves.
    bool applyMoveToTaskPool(TaskPool& pool);
};

}  // namespace snf
