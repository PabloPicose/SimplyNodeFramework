#include "SNFCore/PooledNode.h"

#include "SNFCore/Executor.h"
#include "SNFCore/Strand.h"
#include "SNFCore/TaskPool.h"

#include <stdexcept>

namespace snf {

PooledNode::PooledNode(Node* parent) : Node(parent) {}

PooledNode::~PooledNode() = default;

void PooledNode::update() {}

bool PooledNode::moveToTaskPool(TaskPool* pool)
{
    if (! pool) {
        pool = TaskPool::globalInstance();
    }
    if (! pool) {
        throw std::runtime_error("Application global TaskPool does not exist");
    }
    if (pool->maxThreadCount() == 0) {
        return false;  // shut down (or no threads on this platform)
    }

    Executor* owner = ownerExecutor();
    if (owner->isCurrent()) {
        return applyMoveToTaskPool(*pool);
    }

    postMoveToOwner(this, m_lifetimeBlock, owner, [pool](Node* node) {
        static_cast<PooledNode*>(node)->applyMoveToTaskPool(*pool);
    });
    return true;
}

bool PooledNode::applyMoveToTaskPool(TaskPool& pool)
{
    if (parent()) {
        return false;  // only whole subtrees move
    }
    if (subtreeRequiresEventLoop()) {
        return false;
    }
    // Already on a strand of this pool that is its own: nothing to do. A node
    // that shares its strand with other roots (created inside another pooled
    // node's slot) gets a strand of its own instead.
    if (const auto* strand = dynamic_cast<const detail::Strand*>(ownerExecutor());
        strand && &strand->pool() == &pool && strand->rootNodeCount() == 1) {
        return true;
    }

    detail::Strand* strand = detail::Strand::acquire(pool);
    if (! strand) {
        return false;  // the pool stopped accepting work
    }
    applyMoveTo(strand);
    detail::Strand::release(strand);  // the subtree keeps it in use from now on
    return true;
}

}  // namespace snf
