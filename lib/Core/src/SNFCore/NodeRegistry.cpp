#include "NodeRegistry.h"

namespace snf::detail {

NodeRegistry& NodeRegistry::instance()
{
    static NodeRegistry s_instance;
    return s_instance;
}

void NodeRegistry::registerNode(void* ptr, const std::shared_ptr<NodeLifetimeBlock>& block)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map[ptr] = block;
}

void NodeRegistry::unregisterNode(void* ptr)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map.erase(ptr);
}

std::shared_ptr<NodeLifetimeBlock> NodeRegistry::lookup(void* ptr) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_map.find(ptr);
    if (it == m_map.end()) {
        return nullptr;
    }
    return it->second.lock();
}

}  // namespace snf::detail
