#include "SNFCore/Strand.h"

#include "SNFCore/Application.h"
#include "SNFCore/Logging.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/TaskPool.h"
#include "SNFCore/IoService.h"
#include "SNFCore/TimerService.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace snf::detail {

namespace {

// The strand whose work the current thread is running, or null.
thread_local Strand* t_currentStrand = nullptr;

// Sets t_currentStrand for a scope and restores the previous value.
class CurrentStrandScope
{
public:
    explicit CurrentStrandScope(Strand* strand) : m_previous(t_currentStrand) { t_currentStrand = strand; }
    ~CurrentStrandScope() { t_currentStrand = m_previous; }

    CurrentStrandScope(const CurrentStrandScope&) = delete;
    CurrentStrandScope& operator=(const CurrentStrandScope&) = delete;

private:
    Strand* m_previous;
};

}  // namespace

Strand* Strand::acquire(TaskPool& pool) { return pool.acquireStrand(); }

void Strand::release(Strand* strand)
{
    if (strand) {
        strand->m_pool.releaseStrandReservation(strand);
    }
}

Strand* Strand::current() noexcept { return t_currentStrand; }

Strand::Strand(TaskPool& pool) : m_pool(pool) {}

std::size_t Strand::rootNodeCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rootNodes.size();
}

Strand::~Strand() = default;

void Strand::post(Task task)
{
    if (! task || m_retired.load(std::memory_order_acquire)) {
        return;  // the pool is gone: nothing would ever run it
    }
    bool needsSchedule = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(task));
        if (! m_scheduled && ! m_held) {
            m_scheduled = true;
            m_active.store(true, std::memory_order_release);
            needsSchedule = true;
        }
    }
    if (needsSchedule) {
        schedule();
    }
}

void Strand::schedule()
{
    if (! m_pool.enqueueStrandDrain(this)) {
        // The pool's workers are gone. The queued tasks stay; the remaining
        // nodes are destroyed by TaskPool::shutdown().
        std::lock_guard<std::mutex> lock(m_mutex);
        m_scheduled = false;
        m_active.store(false, std::memory_order_release);
    }
}

void Strand::hold()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_held = true;
}

void Strand::resume()
{
    bool needsSchedule = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_held = false;
        if (! m_scheduled && ! m_queue.empty()) {
            m_scheduled = true;
            m_active.store(true, std::memory_order_release);
            needsSchedule = true;
        }
    }
    if (needsSchedule) {
        schedule();
    }
}

bool Strand::isCurrent() const noexcept { return t_currentStrand == this; }

bool Strand::isActive() const noexcept { return m_active.load(std::memory_order_acquire); }

bool Strand::isRetired() const noexcept { return m_retired.load(std::memory_order_acquire); }

void Strand::enqueueDelete(Node* node) { m_pendingDeletes.push_back(node); }

void Strand::addNode(Node* /*node*/) { m_nodeCount.fetch_add(1, std::memory_order_acq_rel); }

void Strand::removeNode(Node* /*node*/) { m_nodeCount.fetch_sub(1, std::memory_order_acq_rel); }

void Strand::addRootNode(Node* node)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rootNodes.push_back(node);
}

void Strand::removeRootNode(Node* node)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rootNodes.erase(std::remove(m_rootNodes.begin(), m_rootNodes.end(), node), m_rootNodes.end());
}

void Strand::scheduleTimer(Timer* timer, std::chrono::steady_clock::time_point deadline, std::uint64_t generation)
{
    m_pool.timerService().schedule(this, timer, deadline, generation);
}

void Strand::cancelTimer(Timer* timer) { m_pool.timerService().cancel(timer); }

void Strand::registerIO(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback)
{
    m_pool.ioService().registerIO(this, fd, events, std::move(callback));
}

void Strand::modifyIO(int fd, std::uint32_t events) { m_pool.ioService().modifyIO(fd, events); }

void Strand::unregisterIO(int fd) { m_pool.ioService().unregisterIO(fd); }

bool Strand::hasIOWatch(int fd) const { return m_pool.ioService().hasIOWatch(fd); }

void Strand::runPendingDeletes()
{
    // Destructors may deleteLater() other nodes of this strand: loop until
    // nothing is left.
    while (! m_pendingDeletes.empty()) {
        std::vector<Node*> deletes;
        deletes.swap(m_pendingDeletes);
        for (Node* node : deletes) {
            if (NodePtr<Node> alive(node); alive) {
                delete node;
            }
        }
    }
}

void Strand::drain()
{
    {
        const CurrentStrandScope scope(this);

        for (std::size_t ran = 0; ran < kBatchSize; ++ran) {
            Task task;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_queue.empty() || m_held) {
                    break;
                }
                task = std::move(m_queue.front());
                m_queue.pop_front();
            }
            // Caught here, not by the pool: an escaping exception would leave
            // m_scheduled set and the strand would never run again.
            try {
                task();
            } catch (const std::exception& error) {
                sError() << "Strand: task threw an exception: " << error.what();
            } catch (...) {
                sError() << "Strand: task threw a non-standard exception";
            }
        }

        runPendingDeletes();
    }

    bool more = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        more = ! m_queue.empty() && ! m_held;  // a held strand is rescheduled by resume()
        if (! more) {
            m_scheduled = false;
            m_active.store(false, std::memory_order_release);
        }
    }

    if (more) {
        schedule();
    } else {
        m_pool.recycleStrandIfIdle(this);
    }
}

bool Strand::isRecyclable() const
{
    if (m_free || m_reserved || m_nodeCount.load(std::memory_order_acquire) != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return ! m_scheduled && m_queue.empty();
}

void Strand::destroyRemainingNodes()
{
    const CurrentStrandScope scope(this);

    runPendingDeletes();
    const std::vector<Node*> roots = [this] {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_rootNodes;
    }();
    for (Node* node : roots) {
        if (NodePtr<Node> alive(node); alive) {
            delete node;
        }
    }
    runPendingDeletes();

    m_retired.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
}

}  // namespace snf::detail
