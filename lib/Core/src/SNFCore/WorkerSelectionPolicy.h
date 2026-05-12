#pragma once

/**
 * @file WorkerSelectionPolicy.h
 * @brief Strategy interface for selecting a ThreadPool worker.
 * @ingroup SNFCore
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace snf {

/**
 * @struct WorkerLoadSnapshot
 * @ingroup SNFCore
 * @brief Immutable worker metrics used by selection policies.
 */
struct WorkerLoadSnapshot {
    std::thread::id threadId;
    bool hasPendingWork = false;
    std::size_t pendingDeleteCount = 0;
    std::size_t registeredNodesCount = 0;
    std::size_t activeTimerCount = 0;
    std::uint64_t lastIterationDurationNanoseconds = 0;
    std::uint64_t lastActivityAgeNanoseconds = 0;
};

/**
 * @class IWorkerSelectionPolicy
 * @ingroup SNFCore
 * @brief Strategy contract for selecting a worker from load snapshots.
 */
class IWorkerSelectionPolicy
{
public:
    virtual ~IWorkerSelectionPolicy() = default;

    /**
     * @brief Returns the selected worker index from @p snapshots.
     * @return Zero-based index in @p snapshots, or @p snapshots.size() for
     *         "no selection".
     */
    virtual std::size_t selectWorkerIndex(const std::vector<WorkerLoadSnapshot>& snapshots) const = 0;
};

/**
 * @class DefaultWorkerSelectionPolicy
 * @ingroup SNFCore
 * @brief Default heuristic that prefers less loaded and more idle workers.
 */
class DefaultWorkerSelectionPolicy final : public IWorkerSelectionPolicy
{
public:
    std::size_t selectWorkerIndex(const std::vector<WorkerLoadSnapshot>& snapshots) const override;
};

using WorkerSelectionPolicyPtr = std::shared_ptr<const IWorkerSelectionPolicy>;

}  // namespace snf