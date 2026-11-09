#pragma once

/**
 * @file Strand.h
 * @brief Serial executor on top of a TaskPool (internal; used by PooledNode).
 * @ingroup SNFCore
 */

#include <SNFCore/Executor.h>

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace snf {

class TaskPool;

namespace detail {

/**
 * @class Strand
 * @ingroup SNFCore
 * @brief Runs its tasks one at a time, in order, on any free TaskPool worker.
 *
 * Owner of a pooled node subtree. When it has work it schedules one pool
 * task that runs up to kBatchSize tasks, then the deferred deletes, and
 * reschedules itself if more work is left, so a busy strand never keeps a
 * worker from other strands for long.
 *
 * Strands are owned by their TaskPool and never freed while it lives: an
 * idle strand without nodes is recycled for the next acquire(). This keeps a
 * stale `Executor*` (read from the control block of a node that just died)
 * pointing to a live object; a task posted to it that way finds its receiver
 * dead and does nothing.
 */
class Strand final : public Executor
{
public:
    /** @brief Tasks run per pool task before the strand yields the worker. */
    static constexpr std::size_t kBatchSize = 64;

    /**
     * @brief Returns an idle strand of @p pool, reserved for the caller, or
     *        `nullptr` if the pool no longer accepts work.
     *
     * A reserved strand is not recycled even while it has no nodes; call
     * release() once the caller's node(s) live on it (or it is not needed).
     */
    static Strand* acquire(TaskPool& pool);

    /** @brief Drops the reservation taken by acquire(). */
    static void release(Strand* strand);

    /** @brief Returns the strand whose work the calling thread runs, or `nullptr`. */
    static Strand* current() noexcept;

    explicit Strand(TaskPool& pool);
    ~Strand() override;

    Strand(const Strand&) = delete;
    Strand& operator=(const Strand&) = delete;

    /** @brief Returns the pool that runs this strand. */
    TaskPool& pool() const noexcept { return m_pool; }

    /** @brief Returns the number of root nodes this strand owns. */
    std::size_t rootNodeCount() const;

    // ── Executor ────────────────────────────────────────────────────────────
    void post(Task task) override;
    bool isCurrent() const noexcept override;
    bool isActive() const noexcept override;
    bool isRetired() const noexcept override;
    void enqueueDelete(Node* node) override;
    void addNode(Node* node) override;
    void removeNode(Node* node) override;
    void addRootNode(Node* node) override;
    void removeRootNode(Node* node) override;
    void scheduleTimer(Timer* timer, std::chrono::steady_clock::time_point deadline, std::uint64_t generation) override;
    void cancelTimer(Timer* timer) override;
    void registerIO(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback) override;
    void modifyIO(int fd, std::uint32_t events) override;
    void unregisterIO(int fd) override;
    bool hasIOWatch(int fd) const override;

private:
    friend class snf::TaskPool;

    // Pool task: runs one batch, the pending deletes, then reschedules or
    // offers the strand for recycling.
    void drain();
    void schedule();

    // Destroys pending deletes and the remaining root nodes, as if running
    // inside the strand on the calling thread. Called by TaskPool once its
    // workers are joined; the strand is retired afterwards.
    void destroyRemainingNodes();

    void runPendingDeletes();

    // Under the pool's strand mutex: true if it can be recycled right now.
    bool isRecyclable() const;

    TaskPool& m_pool;

    mutable std::mutex m_mutex;
    std::deque<Task> m_queue;
    std::vector<Node*> m_rootNodes;
    std::vector<Node*> m_pendingDeletes;  // only touched inside the strand
    bool m_scheduled = false;             // a drain is queued or running

    std::atomic<bool> m_active{false};  // mirrors m_scheduled for lock-free reads
    std::atomic<bool> m_retired{false};
    std::atomic<std::size_t> m_nodeCount{0};

    // Guarded by the pool's strand mutex.
    bool m_reserved = false;
    bool m_free = false;
};

}  // namespace detail
}  // namespace snf
