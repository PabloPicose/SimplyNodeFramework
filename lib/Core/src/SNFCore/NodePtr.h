#pragma once

/**
 * @file NodePtr.h
 * @brief Generation-tracked safe pointer for Node subclasses.
 * @ingroup SNFCore_Nodes
 */

#include <SNFCore/Application.h>
#include <SNFCore/Node.h>
#include <SNFCore/NodeLifetimeBlock.h>

#include <stdexcept>
#include <type_traits>

namespace snf {

/**
 * @class NodePtr
 * @ingroup SNFCore_Nodes
 * @brief A non-owning, generation-tracked pointer to a `Node` subclass.
 *
 * `NodePtr<T>` stores a raw pointer and the *generation counter* that was
 * assigned to the node at construction time. Before every dereference it
 * asks the `Application` whether the node is still alive and matches the
 * expected generation, preventing use-after-free bugs.
 *
 * A `NodePtr<T>` evaluates to `false` (expired) once the target node has
 * been deleted or its generation has changed.
 *
 * @tparam T A class that derives from `Node`.
 *
 * @code
 * NodePtr<MyNode> ptr(node);
 * if (ptr) {
 *     ptr->doWork(); // safe
 * }
 * @endcode
 */
template <typename T>
class NodePtr
{
    static_assert(std::is_base_of_v<Node, T>, "T must be a subclass of Node");

    T* m_node = nullptr;
    std::shared_ptr<NodeLifetimeBlock> m_block;

public:
    /** @brief Constructs a NodePtr wrapping @p node, sharing its lifetime block. */
    explicit NodePtr(T* node)
        : m_node(node), m_block(node ? node->lifetimeBlock() : nullptr)
    {
    }

    ~NodePtr() = default;

    /** @brief Returns the underlying raw pointer without performing a liveness check. */
    T* data() const { return m_node; }

    /** @brief Returns the underlying raw pointer without performing a liveness check. */
    T* get() const { return m_node; }

    /**
     * @brief Returns `true` if the node memory is accessible.
     *
     * Uses a lock-free atomic load on the control block. Does **not** check
     * whether the node is marked for deferred deletion.
     * Use `isMarkedToDelete()` for that check.
     */
    bool isAlive() const
    {
        return m_block && m_block->alive.load(std::memory_order_acquire);
    }

    /**
     * @brief Returns `true` if the node is marked for deferred deletion or
     *        is no longer memory-accessible.
     *
     * Uses lock-free atomic loads on the control block.
     */
    bool isMarkedToDelete() const
    {
        return ! m_block
            || ! m_block->alive.load(std::memory_order_acquire)
            || m_block->markedForDelete.load(std::memory_order_acquire);
    }

    /**
     * @brief Dereferences the pointer.
     * @return A raw pointer to the node, or `nullptr` if the node is no
     *         longer alive.
     */
    T* operator->() const
    {
        if (m_node && isAlive()) {
            return m_node;
        }
        return nullptr;
    }

    /**
     * @brief Dereferences the pointer.
     * @return A reference to the node.
     * @throws std::runtime_error if the node is no longer alive.
     */
    T& operator*() const
    {
        if (m_node && isAlive()) {
            return *m_node;
        }
        throw std::runtime_error("Node is not alive");
    }

    /** @brief Returns `true` if the node is alive (not deleted and not marked for deletion). */
    explicit operator bool() const { return m_node && isAlive(); }

    /** @brief Returns `true` if both pointers refer to the same node instance. */
    bool operator==(const NodePtr<T>& other) const { return m_node == other.m_node; }

    /** @brief Returns `true` if this pointer refers to @p other. */
    bool operator==(const Node* other) const { return m_node == other; }

    /** @brief Resets the pointer to @p pObj, sharing its lifetime block. */
    void reset(T* pObj = nullptr)
    {
        m_node = pObj;
        m_block = pObj ? pObj->lifetimeBlock() : nullptr;
    }

    /**
     * @brief Performs a `dynamic_cast` to `NodePtr<U>`.
     * @tparam U Target node type.
     * @return A `NodePtr<U>` wrapping the cast result, sharing the same
     *         lifetime block; evaluates to `false` if the cast fails or
     *         the node is not alive.
     *
     * The same control block is reused so the cast does not touch the
     * node's memory when it is already known to be dead.
     */
    template <typename U>
    NodePtr<U> dynamicCast() const
    {
        if (! m_node) {
            return NodePtr<U>(nullptr);
        }
        auto* castPtr = dynamic_cast<U*>(m_node);
        if (! castPtr) {
            return NodePtr<U>(nullptr);
        }
        // Share the existing block rather than calling castPtr->lifetimeBlock()
        // to avoid accessing potentially-freed node memory.
        return NodePtr<U>(castPtr, m_block);
    }
private:
    // Used by dynamicCast() to share an existing control block without
    // accessing the (potentially dead) node object's memory.
    NodePtr(T* node, std::shared_ptr<NodeLifetimeBlock> block)
        : m_node(node), m_block(std::move(block))
    {
    }

    template <typename U>
    friend class NodePtr;
};

}  // namespace snf
