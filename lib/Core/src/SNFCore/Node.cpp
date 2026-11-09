#include "Node.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>

#include "NodeRegistry.h"
#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Executor.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/Strand.h"
#include "SNFCore/TaskPool.h"
#include "SNFCore/ThreadPool.h"

namespace snf {
Node::~Node()
{
    // Signal all NodePtr holders that this node is gone immediately,
    // before touching any framework state. Lock-free: the atomic store
    // is seen by any concurrent NodePtr::isAlive() check.
    m_lifetimeBlock->alive.store(false, std::memory_order_release);

    // Contract: a node is destroyed only inside its owner (use deleteLater()
    // from other threads). Destroying it elsewhere while the owner is running
    // races with the owner ticking the node.
    warnIfTouchedOffOwner("destroyed", "use deleteLater() from other threads");

    // Remove from the global registry so that NodePtr(raw_ptr) constructed
    // after this point correctly gets a null/dead block without dereferencing
    // the freed memory.
    detail::NodeRegistry::instance().unregisterNode(this);

    const auto app = Application::instance();
    Executor* const owner = ownerExecutor();
    if (m_isRoot && owner) {
        owner->removeRootNode(this);
    } else if (m_parent) {
        // Remove from active children
        auto& children = m_parent->m_children;
        children.erase(std::remove(children.begin(), children.end(), this), children.end());

        // Also remove from deferred-delete list to avoid dangling pointer
        // (handles the case where deleteLater() was called on a stack node)
        auto& toDelete = m_parent->m_childrenToDelete;
        toDelete.erase(std::remove(toDelete.begin(), toDelete.end(), this), toDelete.end());
    }
    // Move children out before deleting: each child's destructor removes
    // itself from m_parent->m_children, so iterating the member directly
    // would be undefined behaviour (iterator invalidation).
    auto children = std::move(m_children);
    for (auto* child : children) {
        delete child;
    }

    if (owner) {
        owner->removeNode(this);
    }
    app->unregisterAliveNode(this);
}

void Node::setName(const std::string& name) { m_name = name; }

std::string Node::getName() const { return m_name; }

void Node::addChild(Node* child)
{
    if (! child) {
        std::cerr << "Node::addChild(): Child is nullptr" << std::endl;
        return;
    }
    // if the child is a root node, remove it from its owner loop's root list
    if (child->isRoot()) {
        child->m_isRoot = false;
        if (Executor* childOwner = child->ownerExecutor()) {
            childOwner->removeRootNode(child);
        }
    }
    child->m_parent = this;
    m_children.push_back(child);
}

Node* Node::getChild(const std::size_t index) const
{
    if (index >= m_children.size()) {
        return nullptr;
    }
    return m_children[index];
}

bool Node::isChild(Node* child) const
{
    // if (std::ranges::find(m_children, child) != m_children.end()) {
    //   return true;
    // }
    if (std::find(m_children.begin(), m_children.end(), child) != m_children.end()) {
        return true;
    }
    return false;
}

void Node::deleteLater()
{
    auto app = Application::instance();
    if (! app) {
        throw std::runtime_error("Application instance not exists");
    }

    // Mark the control block immediately so any concurrent NodePtr check
    // (e.g. canInvokeReceiver in a Queued signal) sees this as pending
    // deletion without taking a lock.
    m_lifetimeBlock->markedForDelete.store(true, std::memory_order_release);

    Executor* const owner = ownerExecutor();
    if (owner && ! owner->isCurrent()) {
        // Mark before handing the delete over: once posted, the owner may
        // destroy the node at any moment, so `this` must not be touched
        // after postDeleteToOwner().
        app->markToDelete(this);
        postDeleteToOwner(this, m_lifetimeBlock, owner);
        return;
    }

    app->markToDelete(this);
    if (owner && ! owner->eventLoop()) {
        // Not an EventLoop (a strand): nothing walks the tree to delete
        // marked children, so every deferred delete goes to the executor.
        owner->enqueueDelete(this);
    } else if (m_parent) {
        m_parent->pushDeleteChild(this);
    } else if (owner) {
        owner->enqueueDelete(this);
    } else {
        throw std::runtime_error("Node owner EventLoop does not exist");
    }
}

void Node::postDeleteToOwner(Node* node, std::shared_ptr<NodeLifetimeBlock> block, Executor* executor)
{
    executor->post([executor, node, block = std::move(block)]() {
        if (! block->alive.load(std::memory_order_acquire)) {
            return;
        }
        Executor* owner = block->owner.load(std::memory_order_acquire);
        if (owner && owner != executor) {
            // The node migrated after deleteLater() was requested.
            postDeleteToOwner(node, block, owner);
            return;
        }
        executor->enqueueDelete(node);
    });
}

void Node::postMoveToOwner(Node* node,
                           std::shared_ptr<NodeLifetimeBlock> block,
                           Executor* executor,
                           std::function<void(Node*)> apply)
{
    executor->post([node, block = std::move(block), executor, apply = std::move(apply)]() {
        if (! block->alive.load(std::memory_order_acquire)) {
            return;
        }
        Executor* owner = block->owner.load(std::memory_order_acquire);
        if (owner && owner != executor) {
            // The node migrated again before this request ran: apply it on
            // the current owner instead.
            postMoveToOwner(node, block, owner, apply);
            return;
        }
        // Inside the owner from here on.
        apply(node);
    });
}

void Node::update() { std::cerr << "Pure virtual function called from Node::update()" << std::endl; }

void Node::run()
{
    // delete childs to delete using iterator
    for (auto child : m_childrenToDelete) {
        NodePtr nodePtr(child);
        if (nodePtr) {
            delete child;
        }
    }
    m_childrenToDelete.clear();

    for (auto child : m_children) {
        child->run();
    }
    update();

    m_childrenToDelete.clear();
}

void Node::setParent(Node* parent)
{
    warnIfTouchedOffOwner("re-parented", "change the tree inside the node's owner");
    if (parent) {
        parent->warnIfTouchedOffOwner("given a child", "change the tree inside the parent's owner");
    }

    if (m_parent) {
        for (auto it = m_parent->m_children.begin(); it != m_parent->m_children.end(); ++it) {
            if (*it == this) {
                m_parent->m_children.erase(it);
                break;
            }
        }
    } else if (Executor* owner = ownerExecutor()) {
        owner->removeRootNode(this);
    }
    if (! parent) {
        m_parent = nullptr;
        m_isRoot = true;
        if (Executor* owner = ownerExecutor()) {
            owner->addRootNode(this);
        }
    } else {
        m_isRoot = false;
        parent->addChild(this);
    }
}

Node* Node::parent() const { return m_parent; }

std::size_t Node::childrenCount() const { return m_children.size(); }

bool Node::isRoot() const { return m_isRoot; }

size_t Node::childrenToDeleteCount() const { return m_childrenToDelete.size(); }

std::thread::id Node::ownerThreadId() const
{
    const EventLoop* loop = ownerEventLoop();
    return loop ? loop->ownerThreadId() : std::thread::id();
}

std::thread::id Node::threadId() const { return ownerThreadId(); }

Executor* Node::ownerExecutor() const { return m_lifetimeBlock->owner.load(std::memory_order_acquire); }

EventLoop* Node::ownerEventLoop() const
{
    Executor* executor = ownerExecutor();
    return executor ? executor->eventLoop() : nullptr;
}

void Node::setOwnerExecutor(Executor* executor) { m_lifetimeBlock->owner.store(executor, std::memory_order_release); }

void Node::warnIfTouchedOffOwner(const char* what, const char* hint) const
{
#ifndef NDEBUG
    const Executor* owner = ownerExecutor();
    if (owner && ! owner->isCurrent() && owner->isActive()) {
        std::cerr << "snf::Node: node '" << m_name << "' " << what
                  << " from a thread other than its owner while the owner is running; " << hint << std::endl;
    }
#else
    (void)what;
    (void)hint;
#endif
}

void Node::requireEventLoopOwner(const char* className) const
{
    if (! ownerEventLoop()) {
        throw std::logic_error(std::string("snf::") + className
                               + " needs an EventLoop owner: it cannot be created as a child of a node that lives in a "
                                 "TaskPool");
    }
}

bool Node::requiresEventLoop() const { return false; }

bool Node::subtreeRequiresEventLoop() const
{
    if (requiresEventLoop()) {
        return true;
    }
    return std::any_of(m_children.begin(), m_children.end(), [](const Node* child) {
        return child->subtreeRequiresEventLoop();
    });
}

std::uint64_t Node::generation() const { return m_generation; }

Node::Node(Node* parent) : m_parent(parent)
{
    // Checked before any registration, so a throw leaves nothing behind. A
    // TaskPool worker has no EventLoop: a node can only be created there
    // inside a strand (a PooledNode's slot), and it belongs to that strand:
    // as a child of a node of it, or as a new root of it.
    detail::Strand* const creatingStrand = TaskPool::isWorkerThread() ? detail::Strand::current() : nullptr;
    if (TaskPool::isWorkerThread()) {
        const Executor* parentOwner = m_parent ? m_parent->ownerExecutor() : nullptr;
        const bool valid = m_parent ? (parentOwner && parentOwner->isCurrent()) : (creatingStrand != nullptr);
        if (! valid) {
            throw std::logic_error(
                "snf::Node cannot be created on a TaskPool worker outside a strand: it has no EventLoop to deliver "
                "the node's queued signals and deferred deletes. Create nodes on a thread with an EventLoop (e.g. a "
                "ThreadPool worker), or from inside a PooledNode's slots");
        }
    }

    static std::atomic<std::uint64_t> s_nextGeneration{1};
    m_generation = s_nextGeneration.fetch_add(1, std::memory_order_relaxed);

    m_lifetimeBlock = std::make_shared<NodeLifetimeBlock>();

    // Register in the global registry immediately so that NodePtr(this)
    // can look up the block without dereferencing an arbitrary raw pointer.
    detail::NodeRegistry::instance().registerNode(this, m_lifetimeBlock);

    const auto app = Application::instance();
    if (! app) {
        throw std::runtime_error("Application instance not created");
    }

    Executor* owner = nullptr;
    if (m_parent) {
        m_parent->warnIfTouchedOffOwner("given a child", "create children inside the parent's owner");
        owner = m_parent->ownerExecutor();
        setOwnerExecutor(owner);
        m_parent->addChild(this);
    } else {
        if (creatingStrand) {
            owner = creatingStrand;  // a new root of the strand that creates it
        } else {
            owner = app->getOrCreateCurrentThreadEventLoop();
        }
        setOwnerExecutor(owner);
        m_isRoot = true;
        owner->addRootNode(this);
    }

    if (owner) {
        owner->addNode(this);
    }

    app->registerAliveNode(this);
}

void Node::pushDeleteChild(Node* child)
{
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (*it == child) {
            m_childrenToDelete.push_back(child);
            break;
        }
    }
}

// ── Default (no-op) hooks ───────────────────────────────────────────────────

void Node::onAboutToMoveToThread(EventLoop* /*newLoop*/) {}

void Node::onMovedToThread(EventLoop* /*oldLoop*/) {}

std::shared_ptr<NodeLifetimeBlock> Node::lifetimeBlock() const
{
    return m_lifetimeBlock;
}

// ── Thread migration ────────────────────────────────────────────────────────

bool Node::moveToThread(std::thread::id targetThreadId)
{
    const auto app = Application::instance();
    if (! app) {
        return false;
    }

    EventLoop* targetLoop = app->getEventLoopByThreadId(targetThreadId);
    if (! targetLoop) {
        // Target thread has no EventLoop registered yet.
        return false;
    }
    if (targetLoop->isRetired()) {
        // The target loop will never run again (its ThreadPool shut down).
        return false;
    }

    Executor* currentOwner = ownerExecutor();
    if (! currentOwner) {
        return false;
    }

    // If the parent belongs to a different thread than the target we would
    // break the parent/child affinity invariant.
    if (m_parent && m_parent->ownerThreadId() != targetThreadId) {
        return false;
    }

    if (currentOwner->isCurrent()) {
        // Synchronous path: execute migration immediately.
        applyMoveToHeld(targetLoop);
        return true;
    }

    // Asynchronous path: post to the current owner (forwarded if the node
    // migrates again before the request runs). The preconditions were checked
    // here; the node may have been re-parented since then, and the target may
    // have been retired.
    postMoveToOwner(this, m_lifetimeBlock, currentOwner, [targetLoop](Node* node) {
        if (node->m_parent && node->m_parent->ownerEventLoop() != targetLoop) {
            return;  // never split a child from its parent
        }
        if (targetLoop->isRetired()) {
            return;
        }
        node->applyMoveToHeld(targetLoop);
    });
    return true;
}

bool Node::moveToThreadPool(ThreadPool* pool)
{
    if (! pool) {
        pool = ThreadPool::globalInstance();
    }
    if (! pool) {
        throw std::runtime_error("Application global ThreadPool does not exist");
    }

    // Already on one of the pool's workers: keep it there. The selection
    // policy would otherwise see the node's own load on its current worker
    // and hop it to another one for no reason.
    const std::vector<std::thread::id> workerIds = pool->workerThreadIds();
    if (std::find(workerIds.begin(), workerIds.end(), ownerThreadId()) != workerIds.end()) {
        return true;
    }

    const std::thread::id workerThreadId = pool->preferredWorkerThreadId();
    if (workerThreadId == std::thread::id()) {
        return false;
    }

    if (moveToThread(workerThreadId)) {
        return true;
    }
    return false;
}

void Node::applyMoveToHeld(EventLoop* target)
{
    // The subtree is published to the target node by node and a child's hook
    // may already arm I/O or timers there. Hold the target loop so none of
    // those callbacks runs (and maybe destroys the subtree) on its thread
    // before the whole move is done.
    struct Hold {
        EventLoop* loop;
        explicit Hold(EventLoop* l) : loop(l) { loop->hold(); }
        ~Hold() { loop->resume(); }
        Hold(const Hold&) = delete;
        Hold& operator=(const Hold&) = delete;
    } hold(target);
    applyMoveTo(target);
}

void Node::applyMoveTo(Executor* target)
{
    Executor* old = ownerExecutor();
    if (old == target) {
        // Already owned by it: nothing to do.
        return;
    }

    // 1. Let the subclass release resources on the old owner. The hooks get
    //    EventLoops; a strand side is passed as nullptr.
    onAboutToMoveToThread(target->eventLoop());

    // 2. Update the executors' node registrations.
    if (old) {
        old->removeNode(this);
        if (m_isRoot) {
            old->removeRootNode(this);
        }
    }

    setOwnerExecutor(target);

    target->addNode(this);
    if (m_isRoot) {
        target->addRootNode(this);
    }

    // 3. Let the subclass acquire resources on the new owner.
    onMovedToThread(old ? old->eventLoop() : nullptr);

    // 4. Recurse into children.
    for (Node* child : m_children) {
        child->applyMoveTo(target);
    }
}

}  // namespace snf
