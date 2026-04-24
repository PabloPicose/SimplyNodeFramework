#include "Node.h"

#include <algorithm>
#include <atomic>
#include <iostream>

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"

namespace snf {
Node::~Node()
{
    const auto app = Application::instance();
    if (m_isRoot && m_ownerEventLoop) {
        m_ownerEventLoop->removeRootNode(this);
    } else if (m_parent) {
        // Remove from active children
        auto& children = m_parent->m_children;
        children.erase(std::remove(children.begin(), children.end(), this), children.end());

        // Also remove from deferred-delete list to avoid dangling pointer
        // (handles the case where deleteLater() was called on a stack node)
        auto& toDelete = m_parent->m_childrenToDelete;
        toDelete.erase(std::remove(toDelete.begin(), toDelete.end(), this), toDelete.end());
    }
    for (auto child : m_children) {
        delete child;
    }
    m_children.clear();

    if (m_ownerEventLoop) {
        m_ownerEventLoop->removeNode(this);
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
        if (child->m_ownerEventLoop) {
            child->m_ownerEventLoop->removeRootNode(child);
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

    if (m_ownerEventLoop && ! m_ownerEventLoop->isInThisThread()) {
        m_ownerEventLoop->post([ownerLoop = m_ownerEventLoop, node = this]() { ownerLoop->enqueueDelete(node); });
        app->markToDelete(this);
        return;
    }

    app->markToDelete(this);
    if (m_parent) {
        m_parent->pushDeleteChild(this);
    } else if (m_ownerEventLoop) {
        m_ownerEventLoop->enqueueDelete(this);
    } else {
        throw std::runtime_error("Node owner EventLoop does not exist");
    }
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
    } else if (m_ownerEventLoop) {
        m_ownerEventLoop->removeRootNode(this);
    }
    if (! parent) {
        m_parent = nullptr;
        m_isRoot = true;
        if (m_ownerEventLoop) {
            m_ownerEventLoop->addRootNode(this);
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

std::thread::id Node::ownerThreadId() const { return m_ownerThreadId; }

EventLoop* Node::ownerEventLoop() const { return m_ownerEventLoop; }

std::uint64_t Node::generation() const { return m_generation; }

Node::Node(Node* parent) : m_parent(parent)
{
    static std::atomic<std::uint64_t> s_nextGeneration{1};
    m_generation = s_nextGeneration.fetch_add(1, std::memory_order_relaxed);

    const auto app = Application::instance();
    if (! app) {
        throw std::runtime_error("Application instance not created");
    }

    if (m_parent) {
        m_ownerThreadId = m_parent->ownerThreadId();
        m_ownerEventLoop = m_parent->ownerEventLoop();
        m_parent->addChild(this);
    } else {
        m_ownerThreadId = Application::threadId();
        m_ownerEventLoop = app->getOrCreateCurrentThreadEventLoop();
        m_isRoot = true;
        m_ownerEventLoop->addRootNode(this);
    }

    if (m_ownerEventLoop) {
        m_ownerEventLoop->addNode(this);
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

    EventLoop* currentLoop = m_ownerEventLoop;
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

    // Asynchronous path: post to the current owner thread.
    currentLoop->post([self = NodePtr<Node>(this), targetLoop]() {
        if (self) {
            self->applyMoveToThread(targetLoop);
        }
    });
    return true;
}

void Node::applyMoveToThread(EventLoop* targetLoop)
{
    EventLoop* oldLoop = m_ownerEventLoop;
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

    m_ownerEventLoop = targetLoop;
    m_ownerThreadId  = targetLoop->ownerThreadId();

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
