#include "SNFCore/WorkerSelectionPolicy.h"

#include <limits>
#include <tuple>

namespace snf {

std::size_t DefaultWorkerSelectionPolicy::selectWorkerIndex(const std::vector<WorkerLoadSnapshot>& snapshots) const
{
    if (snapshots.empty()) {
        return snapshots.size();
    }

    // Lexicographic score, lower is better. The node count comes first: it is
    // the stable measure of how much a worker hosts. hasPendingWork is not
    // used, because an armed timer or an I/O watch keeps it permanently true
    // on an otherwise idle worker; it remains in the snapshot for custom
    // policies.
    using Score = std::tuple<std::size_t, std::size_t, std::size_t, std::uint64_t, std::uint64_t>;

    std::size_t bestIndex = snapshots.size();
    Score bestScore{};

    for (std::size_t index = 0; index < snapshots.size(); ++index) {
        const WorkerLoadSnapshot& snapshot = snapshots[index];
        const Score score{
            snapshot.registeredNodesCount,
            snapshot.pendingDeleteCount,
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