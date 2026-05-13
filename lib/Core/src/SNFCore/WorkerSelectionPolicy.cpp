#include "SNFCore/WorkerSelectionPolicy.h"

#include <limits>
#include <tuple>

namespace snf {

std::size_t DefaultWorkerSelectionPolicy::selectWorkerIndex(const std::vector<WorkerLoadSnapshot>& snapshots) const
{
    if (snapshots.empty()) {
        return snapshots.size();
    }

    std::size_t bestIndex = snapshots.size();
    std::tuple<bool, std::size_t, std::size_t, std::size_t, std::uint64_t, std::uint64_t> bestScore{
        true,
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        0,
    };

    for (std::size_t index = 0; index < snapshots.size(); ++index) {
        const WorkerLoadSnapshot& snapshot = snapshots[index];
        const std::size_t pendingWorkScore = snapshot.pendingDeleteCount + (snapshot.hasPendingWork ? 1 : 0);
        const std::tuple<bool, std::size_t, std::size_t, std::size_t, std::uint64_t, std::uint64_t> score{
            snapshot.hasPendingWork,
            pendingWorkScore,
            snapshot.registeredNodesCount,
            snapshot.activeTimerCount,
            snapshot.lastIterationDurationNanoseconds,
            std::numeric_limits<std::uint64_t>::max() - snapshot.lastActivityAgeNanoseconds,
        };

        if (bestIndex == snapshots.size() || score < bestScore) {
            bestIndex = index;
            bestScore = score;
        }
    }

    return bestIndex;
}

}  // namespace snf