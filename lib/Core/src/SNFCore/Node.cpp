#include "Node.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>

#include "NodeRegistry.h"
#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/ThreadPool.h"

namespace snf {
Node::~Node()
{
    // Signal all NodePtr holders that this node is gone immediately,
    // before touching any framework state. Lock-free: the atomic store
    // is seen by any concurrent NodePtr::isAlive() check.
    m_lifetimeBlock->alive.store(false, std::memory_order_release);

#ifndef NDEBUG
    // Contract: a node is destroyed only on its owner thread (use
    // deleteLater() from other threads). Destroying it elsewhere while the
    // owner loop is running races with that loop ticking the node.
    if (const EventLoop* owner = ownerEventLoop(); owner && ! owner->isInThisThread() && owner->isRunning()) {
        std::cerr << "snf::Node: node '" << m_name
                  << "' destroyed from a thread other than its owner while the owner EventLoop is running;"
                     " use deleteLater() from other threads"
                  << std::endl;
    }
#endif

    // Remove from the global registry so that NodePtr(raw_ptr) constructed
    // after this point correctly gets a null/dead block without dereferencing
    // the freed memory.
    detail::NodeRegistry::instance().unregisterNode(this);

    const auto app = Application::instance();
    EventLoop* const ownerLoop = ownerEventLoop();
    if (m_isRoot && ownerLoop) {
        ownerLoop->removeRootNode(this);
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

    if (ownerLoop) {
        ownerLoop->removeNode(this);
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
        if (EventLoop* childLoop = child->ownerEventLoop()) {
            childLoop->removeRootNode(child);
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

    EventLoop* const ownerLoop = ownerEventLoop();
    if (ownerLoop && ! ownerLoop->isInThisThread()) {
        // Mark before handing the delete over: once posted, the owner thread
        // may destroy the node at any moment, so `this` must not be touched
        // after postDeleteToOwner().
        app->markToDelete(this);
        postDeleteToOwner(this, m_lifetimeBlock, ownerLoop);
        return;
    }

    app->markToDelete(this);
    if (m_parent) {
        m_parent->pushDeleteChild(this);
    } else if (ownerLoop) {
        ownerLoop->enqueueDelete(this);
    } else {
        throw std::runtime_error("Node owner EventLoop does not exist");
    }
}

void Node::postDeleteToOwner(Node* node, std::shared_ptr<NodeLifetimeBlock> block, EventLoop* loop)
{
    loop->post([loop, node, block = std::move(block)]() {
        if (! block->alive.load(std::memory_order_acquire)) {
            return;
        }
        EventLoop* owner = block->ownerLoop.load(std::memory_order_acquire);
        if (owner && owner != loop) {
            // The node migrated after deleteLater() was requested.
            postDeleteToOwner(node, block, owner);
            return;
        }
        loop->enqueueDelete(node);
    });
}

void Node::postMoveToOwner(Node* node,
                           std::shared_ptr<NodeLifetimeBlock> block,
                           EventLoop* loop,
                           EventLoop* targetLoop)
{
    loop->post([node, block = std::move(block), loop, targetLoop]() {
        if (! block->alive.load(std::memory_order_acquire)) {
            return;
        }
        EventLoop* owner = block->ownerLoop.load(std::memory_order_acquire);
        if (owner && owner != loop) {
            // The node migrated again before this request ran: apply it on
            // the current owner instead.
            postMoveToOwner(node, block, owner, targetLoop);
            return;
        }
        // On the owner thread from here on. The preconditions were checked on
        // the calling thread; the node may have been re-parented since then.
        // Never split a child from its parent.
        if (node->m_parent && node->m_parent->ownerEventLoop() != targetLoop) {
            return;
        }
        node->applyMoveToThread(targetLoop);
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
    if (m_parent) {
        for (auto it = m_parent->m_children.begin(); it != m_parent->m_children.end(); ++it) {
            if (*it == this) {
                m_parent->m_children.erase(it);
                break;
            }
        }
    } else if (EventLoop* loop = ownerEventLoop()) {
        loop->removeRootNode(this);
    }
    if (! parent) {
        m_parent = nullptr;
        m_isRoot = true;
        if (EventLoop* loop = ownerEventLoop()) {
            loop->addRootNode(this);
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

EventLoop* Node::ownerEventLoop() const { return m_lifetimeBlock->ownerLoop.load(std::memory_order_acquire); }

void Node::setOwnerEventLoop(EventLoop* loop) { m_lifetimeBlock->ownerLoop.store(loop, std::memory_order_release); }

std::uint64_t Node::generation() const { return m_generation; }

Node::Node(Node* parent) : m_parent(parent)
{
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

    EventLoop* ownerLoop = nullptr;
    if (m_parent) {
        ownerLoop = m_parent->ownerEventLoop();
        setOwnerEventLoop(ownerLoop);
        m_parent->addChild(this);
    } else {
        ownerLoop = app->getOrCreateCurrentThreadEventLoop();
        setOwnerEventLoop(ownerLoop);
        m_isRoot = true;
        ownerLoop->addRootNode(this);
    }

    if (ownerLoop) {
        ownerLoop->addNode(this);
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

    EventLoop* currentLoop = ownerEventLoop();
    if (! currentLoop) {
        return false;
    }

    // If the parent belongs to a different thread than the target we would
    // break the parent/child affinity invariant.
    if (m_parent && m_parent->ownerThreadId() != targetThreadId) {
        return false;
    }

    if (currentLoop->isInThisThread()) {
        // Synchronous path: execute migration immediately.
        applyMoveToThread(targetLoop);
        return true;
    }

    // Asynchronous path: post to the current owner thread (forwarded if the
    // node migrates again before the request runs).
    postMoveToOwner(this, m_lifetimeBlock, currentLoop, targetLoop);
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

    const std::thread::id workerThreadId = pool->preferredWorkerThreadId();
    if (workerThreadId == std::thread::id()) {
        return false;
    }

    if (moveToThread(workerThreadId)) {
        return true;
    }
    return false;
}

void Node::applyMoveToThread(EventLoop* targetLoop)
{
    EventLoop* oldLoop = ownerEventLoop();
    if (oldLoop == targetLoop) {
        // Already on that loop — nothing to do.
        return;
    }

    // 1. Let the subclass release resources on the old loop.
    onAboutToMoveToThread(targetLoop);

    // 2. Update EventLoop node registrations.
    if (oldLoop) {
        oldLoop->removeNode(this);
        if (m_isRoot) {
            oldLoop->removeRootNode(this);
        }
    }

    setOwnerEventLoop(targetLoop);

    targetLoop->addNode(this);
    if (m_isRoot) {
        targetLoop->addRootNode(this);
    }

    // 3. Let the subclass acquire resources on the new loop.
    onMovedToThread(oldLoop);

    // 4. Recurse into children.
    for (Node* child : m_children) {
        child->applyMoveToThread(targetLoop);
    }
}

}  // namespace snf
