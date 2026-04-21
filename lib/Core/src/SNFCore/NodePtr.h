#pragma once

#include <SNFCore/Application.h>
#include <SNFCore/Node.h>

#include <stdexcept>
#include <type_traits>

namespace snf {

template <typename T>
class NodePtr
{
    static_assert(std::is_base_of_v<Node, T>, "T must be a subclass of Node");

    T* m_node = nullptr;

public:
    explicit NodePtr(T* node) : m_node(node) {}

    ~NodePtr() = default;

    T* data() const { return m_node; }

    T* get() const { return m_node; }

    /**
     * @brief Gets if the node pointer is still valid. This not checks if the node
     * is about to delete.
     * @returnTrue if the node memory is accessible, false otherwise
     */
    bool isAlive() const { return Application::instance()->isNodeAlive(m_node); }

    /**
     * Gets if the node pointer is marked to be deleted. If the node is marked to
     * be deleted OR the node is not memory accessible, this function returns
     * true.
     * @return True if the node is marked to be deleted OR the node is not memory
     * accessible, false otherwise.
     */
    bool isMarkedToDelete() const { return Application::instance()->isNodeMarkedToDelete(m_node); }

    T* operator->() const
    {
        if (m_node && isAlive()) {
            return m_node;
        }
        return nullptr;
    }

    T& operator*() const
    {
        if (m_node && isAlive()) {
            return *m_node;
        }
        throw std::runtime_error("Node is not alive");
    }

    explicit operator bool() const { return m_node && isAlive(); }

    bool operator==(const NodePtr<T>& other) const { return m_node == other.m_node; }

    bool operator==(const Node* other) const { return m_node == other; }

    void reset(T* pObj = nullptr) { m_node = pObj; }

    //! Dynamic cast to another type
    template <typename U>
    NodePtr<U> dynamicCast() const
    {
        return NodePtr<U>(dynamic_cast<U*>(m_node));
    }
};

}  // namespace snf
