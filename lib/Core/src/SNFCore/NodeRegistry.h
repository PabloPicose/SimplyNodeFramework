#pragma once

/**
 * @file NodeRegistry.h
 * @brief Global registry mapping live Node addresses to their NodeLifetimeBlock.
 * @ingroup SNFCore_Nodes
 *
 * This registry is the cornerstone of safe `NodePtr` construction from a raw
 * pointer whose liveness is unknown:
 *
 * - Every `Node` registers itself here on construction.
 * - Every `Node` unregisters itself on destruction (after marking alive=false).
 * - `NodePtr<T>(T*)` queries the registry instead of dereferencing the raw
 *   pointer, eliminating the heap-use-after-free that would occur if the node
 *   had already been deleted.
 *
 * Thread safety: all public methods are guarded by a mutex and are safe to
 * call concurrently from any thread.
 */

#include "SNFCore/NodeLifetimeBlock.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace snf::detail {

class NodeRegistry
{
public:
    static NodeRegistry& instance();

    void registerNode(void* ptr, const std::shared_ptr<NodeLifetimeBlock>& block);
    void unregisterNode(void* ptr);

    /** Returns a shared_ptr to the block, or nullptr if the node is unknown or the block is gone. */
    std::shared_ptr<NodeLifetimeBlock> lookup(void* ptr) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<void*, std::weak_ptr<NodeLifetimeBlock>> m_map;
};

}  // namespace snf::detail
