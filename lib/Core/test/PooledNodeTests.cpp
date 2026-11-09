#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/Connection.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Executor.h"
#include "SNFCore/IoService.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/PooledNode.h"
#include "SNFCore/Process.h"
#include "SNFCore/Strand.h"
#include "SNFCore/TaskPool.h"
#include "SNFCore/ThreadPool.h"
#include "SNFCore/Timer.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class PooledFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }
    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
};

class PlainNode final : public Node
{
public:
    explicit PlainNode(Node* parent = nullptr) : Node(parent) {}

protected:
    void update() override {}
};

// A pooled node whose behaviour each test plugs in through std::function.
class Worker : public PooledNode
{
public:
    explicit Worker(Node* parent = nullptr) : PooledNode(parent) {}

    void onValue(int value)
    {
        if (onValueHook) {
            onValueHook(*this, value);
        }
    }

    Signal<int> output;
    std::function<void(Worker&, int)> onValueHook;
};

// Records where hooks and the destructor ran.
class ProbeWorker final : public Worker
{
public:
    struct Record {
        std::mutex mutex;
        std::condition_variable cv;
        int destroyed = 0;
        bool destroyedInsideOwner = false;
        std::vector<EventLoop*> aboutToMoveArgs;
        std::vector<EventLoop*> movedArgs;
    };

    explicit ProbeWorker(std::shared_ptr<Record> record, Node* parent = nullptr)
        : Worker(parent), m_record(std::move(record))
    {
    }

    ~ProbeWorker() override
    {
        const Executor* owner = ownerExecutor();
        std::lock_guard<std::mutex> lock(m_record->mutex);
        ++m_record->destroyed;
        m_record->destroyedInsideOwner = owner && owner->isCurrent();
        m_record->cv.notify_all();
    }

protected:
    void onAboutToMoveToThread(EventLoop* newLoop) override
    {
        std::lock_guard<std::mutex> lock(m_record->mutex);
        m_record->aboutToMoveArgs.push_back(newLoop);
    }

    void onMovedToThread(EventLoop* oldLoop) override
    {
        std::lock_guard<std::mutex> lock(m_record->mutex);
        m_record->movedArgs.push_back(oldLoop);
    }

private:
    std::shared_ptr<Record> m_record;
};

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (! predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(100us);
    }
    return true;
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Strand
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, strandRunsTasksOneAtATimeAndInPostingOrder)
{
    TaskPool pool(4);
    detail::Strand* strand = detail::Strand::acquire(pool);
    ASSERT_NE(strand, nullptr);

    std::atomic<int> inside{0};
    std::atomic<int> maxInside{0};
    std::mutex orderMutex;
    std::map<int, std::vector<int>> seenByProducer;

    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < 250; ++i) {
                strand->post([&, p, i]() {
                    const int now = ++inside;
                    int previous = maxInside.load();
                    while (now > previous && ! maxInside.compare_exchange_weak(previous, now)) {
                    }
                    {
                        std::lock_guard<std::mutex> lock(orderMutex);
                        seenByProducer[p].push_back(i);
                    }
                    --inside;
                });
            }
        });
    }
    for (std::thread& producer : producers) {
        producer.join();
    }
    pool.waitForDone();

    EXPECT_EQ(maxInside.load(), 1);
    for (int p = 0; p < 4; ++p) {
        const std::vector<int>& seen = seenByProducer[p];
        ASSERT_EQ(seen.size(), 250u);
        EXPECT_TRUE(std::is_sorted(seen.begin(), seen.end())) << "producer " << p;
    }
    detail::Strand::release(strand);
}

TEST_F(PooledFixture, differentStrandsRunInParallel)
{
    TaskPool pool(2);
    detail::Strand* a = detail::Strand::acquire(pool);
    detail::Strand* b = detail::Strand::acquire(pool);
    ASSERT_NE(a, b);

    // Each task waits for the other: only possible if both run at once.
    std::promise<void> aStarted;
    std::promise<void> bStarted;
    std::shared_future<void> aReady = aStarted.get_future().share();
    std::shared_future<void> bReady = bStarted.get_future().share();
    std::atomic<int> metEachOther{0};

    a->post([&, bReady]() {
        aStarted.set_value();
        if (bReady.wait_for(1s) == std::future_status::ready) {
            metEachOther.fetch_add(1);
        }
    });
    b->post([&, aReady]() {
        bStarted.set_value();
        if (aReady.wait_for(1s) == std::future_status::ready) {
            metEachOther.fetch_add(1);
        }
    });
    pool.waitForDone();

    EXPECT_EQ(metEachOther.load(), 2);
    detail::Strand::release(a);
    detail::Strand::release(b);
}

TEST_F(PooledFixture, strandIsCurrentOnlyInsideItsOwnTasks)
{
    TaskPool pool(2);
    detail::Strand* a = detail::Strand::acquire(pool);
    detail::Strand* b = detail::Strand::acquire(pool);
    std::atomic<bool> aCurrentInA{false};
    std::atomic<bool> bCurrentInA{true};

    a->post([&]() {
        aCurrentInA.store(a->isCurrent());
        bCurrentInA.store(b->isCurrent());
    });
    pool.waitForDone();

    EXPECT_TRUE(aCurrentInA.load());
    EXPECT_FALSE(bCurrentInA.load());
    EXPECT_FALSE(a->isCurrent());
    EXPECT_EQ(a->eventLoop(), nullptr);
    detail::Strand::release(a);
    detail::Strand::release(b);
}

TEST_F(PooledFixture, exceptionInAStrandTaskDoesNotStallTheStrand)
{
    TaskPool pool(1);
    detail::Strand* strand = detail::Strand::acquire(pool);
    std::atomic<int> ranAfter{0};

    strand->post([]() { throw std::runtime_error("boom"); });
    strand->post([&]() { ranAfter.fetch_add(1); });
    pool.waitForDone();
    strand->post([&]() { ranAfter.fetch_add(1); });  // still schedulable afterwards
    pool.waitForDone();

    EXPECT_EQ(ranAfter.load(), 2);
    detail::Strand::release(strand);
}

TEST_F(PooledFixture, aBusyStrandYieldsToOtherStrandsOnASingleWorker)
{
    TaskPool pool(1);
    detail::Strand* busy = detail::Strand::acquire(pool);
    detail::Strand* other = detail::Strand::acquire(pool);

    // Hold the only worker until everything is queued.
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    ASSERT_TRUE(pool.start([released]() { released.wait(); }));

    constexpr int kBusyTasks = 1000;
    std::atomic<int> busyDone{0};
    std::atomic<int> busyDoneWhenOtherRan{-1};
    for (int i = 0; i < kBusyTasks; ++i) {
        busy->post([&]() { busyDone.fetch_add(1); });
    }
    other->post([&]() { busyDoneWhenOtherRan.store(busyDone.load()); });

    release.set_value();
    pool.waitForDone();

    EXPECT_EQ(busyDone.load(), kBusyTasks);
    EXPECT_GE(busyDoneWhenOtherRan.load(), 0);
    EXPECT_LT(busyDoneWhenOtherRan.load(), kBusyTasks);
    detail::Strand::release(busy);
    detail::Strand::release(other);
}

TEST_F(PooledFixture, idleStrandsWithoutNodesAreRecycled)
{
    TaskPool pool(1);
    detail::Strand* first = detail::Strand::acquire(pool);
    detail::Strand::release(first);
    detail::Strand* second = detail::Strand::acquire(pool);

    EXPECT_EQ(second, first);
    EXPECT_EQ(pool.strandCount(), 1u);
    detail::Strand::release(second);
}

// ═════════════════════════════════════════════════════════════════════════════
// PooledNode: execution
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, moveToTaskPoolRunsSlotsOnPoolWorkersInsideTheNodeExecutor)
{
    TaskPool pool(2);
    const std::vector<std::thread::id> workerIds = pool.workerThreadIds();
    const std::set<std::thread::id> workers(workerIds.begin(), workerIds.end());

    auto* worker = new Worker();
    std::mutex mutex;
    std::thread::id ranOn;
    bool ranInsideOwner = false;
    worker->onValueHook = [&](const Worker& self, int) {
        std::lock_guard<std::mutex> lock(mutex);
        ranOn = std::this_thread::get_id();
        ranInsideOwner = self.ownerExecutor()->isCurrent();
    };

    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);  // default: Auto

    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    EXPECT_EQ(worker->ownerEventLoop(), nullptr);
    EXPECT_EQ(worker->ownerThreadId(), std::thread::id());
    ASSERT_NE(worker->ownerExecutor(), nullptr);
    EXPECT_EQ(worker->ownerExecutor()->eventLoop(), nullptr);

    input.emit(1);
    pool.waitForDone();

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_TRUE(workers.count(ranOn) > 0);
    EXPECT_TRUE(ranInsideOwner);
}

TEST_F(PooledFixture, slotsOfOnePooledNodeNeverOverlapAndKeepPerEmitterOrder)
{
    TaskPool pool(4);
    auto* worker = new Worker();
    std::atomic<int> inside{0};
    std::atomic<int> maxInside{0};
    std::map<int, std::vector<int>> seen;  // only touched inside the node: no lock needed

    worker->onValueHook = [&](Worker&, int value) {
        const int now = ++inside;
        int previous = maxInside.load();
        while (now > previous && ! maxInside.compare_exchange_weak(previous, now)) {
        }
        seen[value / 1000].push_back(value % 1000);
        --inside;
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    std::vector<std::thread> emitters;
    for (int e = 0; e < 4; ++e) {
        emitters.emplace_back([&, e]() {
            for (int i = 0; i < 300; ++i) {
                input.emit(e * 1000 + i);
            }
        });
    }
    for (std::thread& emitter : emitters) {
        emitter.join();
    }
    pool.waitForDone();

    EXPECT_EQ(maxInside.load(), 1);
    for (int e = 0; e < 4; ++e) {
        ASSERT_EQ(seen[e].size(), 300u);
        EXPECT_TRUE(std::is_sorted(seen[e].begin(), seen[e].end())) << "emitter " << e;
    }
}

TEST_F(PooledFixture, severalPooledNodesRunAtTheSameTime)
{
    TaskPool pool(2);
    auto* a = new Worker();
    auto* b = new Worker();
    std::promise<void> aIn;
    std::promise<void> bIn;
    std::shared_future<void> aReady = aIn.get_future().share();
    std::shared_future<void> bReady = bIn.get_future().share();
    std::atomic<int> met{0};

    a->onValueHook = [&, bReady](Worker&, int) {
        aIn.set_value();
        met += bReady.wait_for(1s) == std::future_status::ready ? 1 : 0;
    };
    b->onValueHook = [&, aReady](Worker&, int) {
        bIn.set_value();
        met += aReady.wait_for(1s) == std::future_status::ready ? 1 : 0;
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(a), &Worker::onValue);
    input.connect(NodePtr<Worker>(b), &Worker::onValue);
    ASSERT_TRUE(a->moveToTaskPool(&pool));
    ASSERT_TRUE(b->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();

    EXPECT_EQ(met.load(), 2);
}

TEST_F(PooledFixture, autoSignalInsideTheNodeToItsOwnChildIsDirect)
{
    TaskPool pool(2);
    auto* parent = new Worker();
    auto* child = new Worker(parent);
    std::atomic<bool> childRanInsideParentSlot{false};
    bool inParentSlot = false;  // only touched inside the node

    child->onValueHook = [&](Worker&, int) { childRanInsideParentSlot.store(inParentSlot); };
    parent->output.connect(NodePtr<Worker>(child), &Worker::onValue);
    parent->onValueHook = [&](Worker& self, int value) {
        inParentSlot = true;
        self.output.emit(value);  // same executor: synchronous
        inParentSlot = false;
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(parent), &Worker::onValue);
    ASSERT_TRUE(parent->moveToTaskPool(&pool));
    EXPECT_EQ(child->ownerExecutor(), parent->ownerExecutor());

    input.emit(1);
    pool.waitForDone();

    EXPECT_TRUE(childRanInsideParentSlot.load());
}

TEST_F(PooledFixture, pooledNodeSignalsReachAMainThreadNodeOnMain)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    auto* receiver = new PlainNode();
    EventLoop* mainLoop = receiver->ownerEventLoop();
    std::atomic<int> received{0};
    std::thread::id receivedOn;

    worker->output.connect(NodePtr<PlainNode>(receiver), [&](PlainNode&, int value) {
        receivedOn = std::this_thread::get_id();
        received.store(value);
    });
    worker->onValueHook = [](Worker& self, int value) { self.output.emit(value * 2); };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(21);
    pool.waitForDone();
    EXPECT_EQ(received.load(), 0);  // queued to main, not run on the worker

    mainLoop->runPendingWork();
    EXPECT_EQ(received.load(), 42);
    EXPECT_EQ(receivedOn, std::this_thread::get_id());
    delete receiver;
}

TEST_F(PooledFixture, timerOnMainCanDrivePooledNode)
{
    TaskPool pool(1);
    auto* worker = new Worker();
    std::atomic<int> ticks{0};
    worker->onValueHook = [&](Worker&, int) { ticks.fetch_add(1); };
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    auto* timer = new Timer();
    timer->setSingleShot(true);
    timer->timeout.connect(NodePtr<Worker>(worker), [](Worker& self) { self.onValue(0); });
    timer->start(1ms);

    ASSERT_TRUE(waitUntil([&]() {
        app->loopOnce();
        return ticks.load() > 0;
    }));
    pool.waitForDone();
    delete timer;
}

// ═════════════════════════════════════════════════════════════════════════════
// PooledNode: tree, creation and deletion inside the pool
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, childCreatedInsideASlotSharesTheStrand)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    std::atomic<bool> sameExecutor{false};
    std::atomic<std::size_t> children{0};

    worker->onValueHook = [&](Worker& self, int) {
        auto* child = new PlainNode(&self);
        sameExecutor.store(child->ownerExecutor() == self.ownerExecutor());
        children.store(self.childrenCount());
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();

    EXPECT_TRUE(sameExecutor.load());
    EXPECT_EQ(children.load(), 1u);
}

TEST_F(PooledFixture, rootNodeCreatedInsideASlotJoinsTheStrandAndCanMoveToItsOwn)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    std::atomic<bool> joinedTheStrand{false};
    std::atomic<bool> isRoot{false};
    std::atomic<bool> moved{false};
    std::atomic<bool> gotItsOwnStrand{false};
    worker->onValueHook = [&](const Worker& self, int) {
        auto* spawned = new Worker();  // a new root, created inside the strand
        joinedTheStrand.store(spawned->ownerExecutor() == self.ownerExecutor());
        isRoot.store(spawned->isRoot());
        moved.store(spawned->moveToTaskPool(&pool));
        gotItsOwnStrand.store(spawned->ownerExecutor() != self.ownerExecutor());
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();

    EXPECT_TRUE(joinedTheStrand.load());
    EXPECT_TRUE(isRoot.load());
    EXPECT_TRUE(moved.load());
    EXPECT_TRUE(gotItsOwnStrand.load());
    EXPECT_EQ(pool.strandCount(), 2u);
}

TEST_F(PooledFixture, processCreatedInsideASlotThrows)
{
    TaskPool pool(1);
    auto* worker = new Worker();
    std::atomic<bool> threw{false};
    std::atomic<std::size_t> childrenAfter{99};
    worker->onValueHook = [&](Worker& self, int) {
        try {
            new Process(&self);
        } catch (const std::logic_error&) {
            threw.store(true);
        }
        childrenAfter.store(self.childrenCount());
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();

    EXPECT_TRUE(threw.load());
    EXPECT_EQ(childrenAfter.load(), 0u);  // the failed Process left nothing behind
}

TEST_F(PooledFixture, deleteLaterInsideASlotDestroysTheNodeInsideItsExecutor)
{
    TaskPool pool(2);
    auto record = std::make_shared<ProbeWorker::Record>();
    auto* worker = new ProbeWorker(record);
    worker->onValueHook = [](Worker& self, int) { self.deleteLater(); };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();

    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->destroyed, 1);
    EXPECT_TRUE(record->destroyedInsideOwner);
}

TEST_F(PooledFixture, deleteLaterFromMainDestroysThePooledNodeInsideItsExecutor)
{
    TaskPool pool(2);
    auto record = std::make_shared<ProbeWorker::Record>();
    auto* worker = new ProbeWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    worker->deleteLater();
    pool.waitForDone();

    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->destroyed, 1);
    EXPECT_TRUE(record->destroyedInsideOwner);
}

TEST_F(PooledFixture, deleteLaterOnAChildInsideTheNodeDestroysOnlyTheChild)
{
    TaskPool pool(2);
    auto* parent = new Worker();
    auto childRecord = std::make_shared<ProbeWorker::Record>();
    auto* child = new ProbeWorker(childRecord, parent);
    std::atomic<std::size_t> childrenAfter{99};

    parent->onValueHook = [&](const Worker& self, int value) {
        if (value == 0) {
            child->deleteLater();
        } else {
            childrenAfter.store(self.childrenCount());
        }
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(parent), &Worker::onValue);
    ASSERT_TRUE(parent->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();
    input.emit(1);
    pool.waitForDone();

    {
        std::lock_guard<std::mutex> lock(childRecord->mutex);
        EXPECT_EQ(childRecord->destroyed, 1);
        EXPECT_TRUE(childRecord->destroyedInsideOwner);
    }
    EXPECT_EQ(childrenAfter.load(), 0u);
}

// ═════════════════════════════════════════════════════════════════════════════
// PooledNode: moveToTaskPool preconditions and moving back
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, moveToTaskPoolRejectsASubtreeThatNeedsAnEventLoop)
{
    TaskPool pool(1);
    auto* worker = new Worker();
    new Process(worker);
    EventLoop* mainLoop = worker->ownerEventLoop();

    EXPECT_FALSE(worker->moveToTaskPool(&pool));
    EXPECT_EQ(worker->ownerEventLoop(), mainLoop);

    delete worker;
}

TEST_F(PooledFixture, moveToTaskPoolRejectsANodeWithAParent)
{
    TaskPool pool(1);
    auto* parent = new PlainNode();
    auto* worker = new Worker(parent);

    EXPECT_FALSE(worker->moveToTaskPool(&pool));
    EXPECT_EQ(worker->ownerEventLoop(), parent->ownerEventLoop());

    delete parent;
}

TEST_F(PooledFixture, moveToTaskPoolRejectsAShutDownPool)
{
    TaskPool pool(1);
    pool.shutdown();
    auto* worker = new Worker();

    EXPECT_FALSE(worker->moveToTaskPool(&pool));
    EXPECT_NE(worker->ownerEventLoop(), nullptr);

    delete worker;
}

TEST_F(PooledFixture, moveToTaskPoolTwiceKeepsTheSameStrand)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    Executor* first = worker->ownerExecutor();

    EXPECT_TRUE(worker->moveToTaskPool(&pool));
    pool.waitForDone();

    EXPECT_EQ(worker->ownerExecutor(), first);
    EXPECT_EQ(pool.strandCount(), 1u);
}

TEST_F(PooledFixture, moveToTaskPoolUsesTheGlobalTaskPoolByDefault)
{
    auto* worker = new Worker();
    std::atomic<bool> ran{false};
    worker->onValueHook = [&](Worker&, int) { ran.store(true); };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);

    ASSERT_TRUE(worker->moveToTaskPool());
    EXPECT_EQ(app->taskPool()->strandCount(), 1u);
    input.emit(0);
    app->taskPool()->waitForDone();

    EXPECT_TRUE(ran.load());
}

TEST_F(PooledFixture, moveToTaskPoolFromAnotherThreadIsAppliedByTheOwner)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    EventLoop* mainLoop = worker->ownerEventLoop();

    std::thread caller([&]() { EXPECT_TRUE(worker->moveToTaskPool(&pool)); });
    caller.join();
    EXPECT_EQ(worker->ownerEventLoop(), mainLoop);  // not applied yet

    mainLoop->runPendingWork();
    EXPECT_EQ(worker->ownerEventLoop(), nullptr);
    EXPECT_EQ(pool.strandCount(), 1u);
}

TEST_F(PooledFixture, migrationHooksGetNullForTheStrandSide)
{
    TaskPool pool(2);
    auto record = std::make_shared<ProbeWorker::Record>();
    auto* worker = new ProbeWorker(record);
    EventLoop* mainLoop = worker->ownerEventLoop();
    const std::thread::id mainId = std::this_thread::get_id();

    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    ASSERT_TRUE(worker->moveToThread(mainId));  // posted to the strand
    pool.waitForDone();

    std::lock_guard<std::mutex> lock(record->mutex);
    ASSERT_EQ(record->aboutToMoveArgs.size(), 2u);
    EXPECT_EQ(record->aboutToMoveArgs[0], nullptr);   // main -> strand
    EXPECT_EQ(record->movedArgs[0], mainLoop);
    EXPECT_EQ(record->aboutToMoveArgs[1], mainLoop);  // strand -> main
    EXPECT_EQ(record->movedArgs[1], nullptr);
}

TEST_F(PooledFixture, pooledNodeCanMoveBackToAFixedThread)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    EventLoop* mainLoop = worker->ownerEventLoop();
    std::atomic<int> received{0};
    std::thread::id receivedOn;
    worker->onValueHook = [&](Worker&, int value) {
        receivedOn = std::this_thread::get_id();
        received.store(value);
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);

    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    ASSERT_TRUE(worker->moveToThread(std::this_thread::get_id()));
    pool.waitForDone();
    EXPECT_EQ(worker->ownerEventLoop(), mainLoop);

    input.emit(5);  // Auto, same thread: direct again
    EXPECT_EQ(received.load(), 5);
    EXPECT_EQ(receivedOn, std::this_thread::get_id());

    // The strand it left is free again.
    auto* other = new Worker();
    ASSERT_TRUE(other->moveToTaskPool(&pool));
    EXPECT_EQ(pool.strandCount(), 1u);
    delete worker;
}

// ═════════════════════════════════════════════════════════════════════════════
// PooledNode: lifetime and shutdown
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, strandsOfDestroyedNodesAreReused)
{
    TaskPool pool(2);
    for (int i = 0; i < 20; ++i) {
        auto* worker = new Worker();
        ASSERT_TRUE(worker->moveToTaskPool(&pool));
        worker->deleteLater();
        pool.waitForDone();
    }
    EXPECT_EQ(pool.strandCount(), 1u);
}

TEST_F(PooledFixture, shutdownRunsQueuedDeliveriesThenDestroysPooledNodes)
{
    auto record = std::make_shared<ProbeWorker::Record>();
    std::atomic<int> received{0};
    {
        TaskPool pool(1);
        auto* worker = new ProbeWorker(record);
        worker->onValueHook = [&](Worker&, int) { received.fetch_add(1); };
        Signal<int> input;
        input.connect(NodePtr<Worker>(worker), &Worker::onValue);
        ASSERT_TRUE(worker->moveToTaskPool(&pool));
        for (int i = 0; i < 10; ++i) {
            input.emit(i);
        }
        pool.shutdown();

        EXPECT_EQ(received.load(), 10);
        std::lock_guard<std::mutex> lock(record->mutex);
        EXPECT_EQ(record->destroyed, 1);
        EXPECT_TRUE(record->destroyedInsideOwner);
    }
}

TEST_F(PooledFixture, applicationDestructionDestroysNodesOnTheGlobalTaskPool)
{
    auto record = std::make_shared<ProbeWorker::Record>();
    auto* worker = new ProbeWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool());

    delete app;
    app = nullptr;

    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->destroyed, 1);
}

TEST_F(PooledFixture, emitAfterTheReceiverIsDestroyedIsDropped)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    std::atomic<int> received{0};
    worker->onValueHook = [&](Worker&, int) { received.fetch_add(1); };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    worker->deleteLater();
    pool.waitForDone();
    input.emit(1);
    pool.waitForDone();

    EXPECT_EQ(received.load(), 0);
}

TEST_F(PooledFixture, stressManyPooledNodesCreatedEmittedAndDestroyedConcurrently)
{
    TaskPool pool(4);
    std::atomic<int> received{0};

    for (int round = 0; round < 20; ++round) {
        std::vector<Worker*> workers;
        Signal<int> input;
        for (int i = 0; i < 16; ++i) {
            auto* worker = new Worker();
            worker->onValueHook = [&](Worker& self, int value) {
                received.fetch_add(1);
                if (value % 7 == 0) {
                    self.output.emit(value);
                }
            };
            input.connect(NodePtr<Worker>(worker), &Worker::onValue);
            ASSERT_TRUE(worker->moveToTaskPool(&pool));
            workers.push_back(worker);
        }

        std::vector<std::thread> emitters;
        for (int e = 0; e < 3; ++e) {
            emitters.emplace_back([&]() {
                for (int i = 0; i < 50; ++i) {
                    input.emit(i);
                }
            });
        }
        // Destroy half of them while the emitters run.
        for (std::size_t i = 0; i < workers.size(); i += 2) {
            workers[i]->deleteLater();
        }
        for (std::thread& emitter : emitters) {
            emitter.join();
        }
        for (std::size_t i = 1; i < workers.size(); i += 2) {
            workers[i]->deleteLater();
        }
        pool.waitForDone();
    }

    EXPECT_GT(received.load(), 0);
    EXPECT_LE(pool.strandCount(), 16u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Debug affinity checker
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, touchingAPooledNodeFromOutsideWhileItRunsWarnsInDebug)
{
#ifdef NDEBUG
    GTEST_SKIP() << "The warning is only emitted in Debug builds";
#endif
    TaskPool pool(1);
    auto* worker = new Worker();
    auto* child = new PlainNode(worker);
    std::promise<void> inside;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    worker->onValueHook = [&, released](Worker&, int) {
        inside.set_value();
        released.wait();
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    inside.get_future().wait();
    testing::internal::CaptureStderr();
    child->setName("renamed");
    child->setParent(nullptr);  // from main while the strand runs
    const std::string output = testing::internal::GetCapturedStderr();
    release.set_value();
    pool.waitForDone();

    EXPECT_NE(output.find("from a thread other than its owner"), std::string::npos) << output;
}

// ═════════════════════════════════════════════════════════════════════════════
// Remaining paths found by coverage
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, postToAStrandOfAShutDownPoolIsDropped)
{
    TaskPool pool(1);
    detail::Strand* strand = detail::Strand::acquire(pool);
    detail::Strand::release(nullptr);  // no-op
    pool.shutdown();

    std::atomic<bool> ran{false};
    strand->post([&]() { ran.store(true); });

    EXPECT_TRUE(strand->isRetired());
    EXPECT_FALSE(strand->isActive());
    EXPECT_FALSE(ran.load());
    EXPECT_EQ(detail::Strand::acquire(pool), nullptr);
}

TEST_F(PooledFixture, deliveriesQueuedWhileShutdownDestroysNodesAreDropped)
{
    // A's destructor emits to B while shutdown() destroys the pooled nodes:
    // no worker is left to run B's strand, so the delivery is dropped and B
    // is destroyed as well.
    class EmittingOnDestruction final : public Worker
    {
    public:
        ~EmittingOnDestruction() override { output.emit(1); }
    };

    auto bRecord = std::make_shared<ProbeWorker::Record>();
    std::atomic<int> bReceived{0};
    {
        TaskPool pool(1);
        auto* a = new EmittingOnDestruction();
        auto* b = new ProbeWorker(bRecord);
        b->onValueHook = [&](Worker&, int) { bReceived.fetch_add(1); };
        a->output.connect(NodePtr<Worker>(b), &Worker::onValue);
        ASSERT_TRUE(a->moveToTaskPool(&pool));  // first strand: destroyed first
        ASSERT_TRUE(b->moveToTaskPool(&pool));
        pool.shutdown();
    }

    EXPECT_EQ(bReceived.load(), 0);
    std::lock_guard<std::mutex> lock(bRecord->mutex);
    EXPECT_EQ(bRecord->destroyed, 1);
}

TEST_F(PooledFixture, nonStandardExceptionInAStrandTaskDoesNotStallTheStrand)
{
    TaskPool pool(1);
    detail::Strand* strand = detail::Strand::acquire(pool);
    std::atomic<bool> ranAfter{false};

    strand->post([]() { throw 42; });
    strand->post([&]() { ranAfter.store(true); });
    pool.waitForDone();

    EXPECT_TRUE(ranAfter.load());
    detail::Strand::release(strand);
}

TEST_F(PooledFixture, deleteLaterOnParentThenChildInOneTurnDestroysEachOnce)
{
    TaskPool pool(1);
    auto parentRecord = std::make_shared<ProbeWorker::Record>();
    auto childRecord = std::make_shared<ProbeWorker::Record>();
    auto* parent = new ProbeWorker(parentRecord);
    auto* child = new ProbeWorker(childRecord, parent);
    parent->onValueHook = [child](Worker& self, int) {
        self.deleteLater();   // destroys the child too...
        child->deleteLater(); // ...so this pending delete finds it dead
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(parent), &Worker::onValue);
    ASSERT_TRUE(parent->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();

    std::lock_guard<std::mutex> parentLock(parentRecord->mutex);
    std::lock_guard<std::mutex> childLock(childRecord->mutex);
    EXPECT_EQ(parentRecord->destroyed, 1);
    EXPECT_EQ(childRecord->destroyed, 1);
}

TEST_F(PooledFixture, aStrandWithQueuedWorkIsNotHandedOutAgainUntilIdle)
{
    TaskPool pool(2);
    detail::Strand* busy = detail::Strand::acquire(pool);
    std::promise<void> started;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    busy->post([&started, released]() {
        started.set_value();
        released.wait();
    });
    started.get_future().wait();

    detail::Strand::release(busy);  // no nodes, but still running
    detail::Strand* other = detail::Strand::acquire(pool);
    EXPECT_NE(other, busy);

    release.set_value();
    pool.waitForDone();
    detail::Strand::release(other);
    detail::Strand* again = detail::Strand::acquire(pool);
    EXPECT_TRUE(again == busy || again == other);
    EXPECT_EQ(pool.strandCount(), 2u);
    detail::Strand::release(again);
}

TEST_F(PooledFixture, pooledNodeMovesFromOneTaskPoolToAnother)
{
    TaskPool first(1);
    TaskPool second(1);
    const std::thread::id secondWorker = second.workerThreadIds().at(0);
    auto* worker = new Worker();
    std::atomic<bool> ranOnSecond{false};
    worker->onValueHook = [&](Worker&, int) { ranOnSecond.store(std::this_thread::get_id() == secondWorker); };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);

    ASSERT_TRUE(worker->moveToTaskPool(&first));
    Executor* firstStrand = worker->ownerExecutor();
    ASSERT_TRUE(worker->moveToTaskPool(&second));  // posted to the first strand
    first.waitForDone();
    EXPECT_NE(worker->ownerExecutor(), firstStrand);

    input.emit(0);
    second.waitForDone();
    EXPECT_TRUE(ranOnSecond.load());
    worker->deleteLater();
    second.waitForDone();
}

TEST_F(PooledFixture, asyncMoveToTaskPoolIsDroppedIfThePoolShutsDownBeforeItApplies)
{
    TaskPool pool(1);
    auto* worker = new Worker();
    EventLoop* mainLoop = worker->ownerEventLoop();

    std::thread caller([&]() { EXPECT_TRUE(worker->moveToTaskPool(&pool)); });
    caller.join();
    pool.shutdown();
    mainLoop->runPendingWork();

    EXPECT_EQ(worker->ownerEventLoop(), mainLoop);
    delete worker;
}

// ═════════════════════════════════════════════════════════════════════════════
// Phase 4: Timers owned by a pooled node
// ═════════════════════════════════════════════════════════════════════════════

namespace {

// Pooled node with a Timer child; records every timeout.
class TickingWorker final : public PooledNode
{
public:
    struct Record {
        std::mutex mutex;
        std::condition_variable cv;
        int ticks = 0;
        int ticksOffOwner = 0;
        int emitsOffOwner = 0;  // timeout emitted outside the timer's owner
        std::set<std::thread::id> threads;
    };

    explicit TickingWorker(std::shared_ptr<Record> record, Node* parent = nullptr)
        : PooledNode(parent), timer(new Timer(this)), m_record(std::move(record))
    {
        timer->timeout.connect(NodePtr<TickingWorker>(this), &TickingWorker::onTick);
        // Direct, without a receiver: runs wherever timeout is emitted, which
        // must always be inside the timer's owner.
        Timer* const ownTimer = timer;
        std::shared_ptr<Record> shared = m_record;
        timer->timeout.connect([ownTimer, shared]() {
            if (! ownTimer->ownerExecutor()->isCurrent()) {
                std::lock_guard<std::mutex> lock(shared->mutex);
                ++shared->emitsOffOwner;
            }
        });
    }

    TickingWorker(const TickingWorker&) = delete;
    TickingWorker& operator=(const TickingWorker&) = delete;

    void onTick()
    {
        const bool inside = ownerExecutor()->isCurrent();
        std::lock_guard<std::mutex> lock(m_record->mutex);
        ++m_record->ticks;
        if (! inside) {
            ++m_record->ticksOffOwner;
        }
        m_record->threads.insert(std::this_thread::get_id());
        m_record->cv.notify_all();
        if (stopAfter > 0 && m_record->ticks >= stopAfter) {
            timer->stop();
        }
    }

    Timer* timer;
    int stopAfter = 0;  // only touched inside the node

private:
    std::shared_ptr<Record> m_record;
};

bool waitForTicks(TickingWorker::Record& record, int ticks, std::chrono::milliseconds timeout = 2s)
{
    std::unique_lock<std::mutex> lock(record.mutex);
    return record.cv.wait_for(lock, timeout, [&]() { return record.ticks >= ticks; });
}

int ticksOf(TickingWorker::Record& record)
{
    std::lock_guard<std::mutex> lock(record.mutex);
    return record.ticks;
}

}  // namespace

TEST_F(PooledFixture, aTimerNoLongerRequiresAnEventLoop)
{
    auto* timer = new Timer();
    EXPECT_FALSE(timer->requiresEventLoop());
    delete timer;

    auto* process = new Process();
    EXPECT_TRUE(process->requiresEventLoop());
    delete process;
}

TEST_F(PooledFixture, timerStartedOnMainKeepsRunningAfterMoveToTaskPool)
{
    TaskPool pool(2);
    const std::vector<std::thread::id> ids = pool.workerThreadIds();
    const std::set<std::thread::id> workers(ids.begin(), ids.end());
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    worker->timer->setSingleShot(true);
    worker->timer->start(5ms);

    ASSERT_TRUE(worker->moveToTaskPool(&pool));  // the timer moves with it

    ASSERT_TRUE(waitForTicks(*record, 1));
    pool.waitForDone();
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->ticks, 1);
    EXPECT_EQ(record->ticksOffOwner, 0);
    EXPECT_EQ(record->emitsOffOwner, 0);
    ASSERT_EQ(record->threads.size(), 1u);
    EXPECT_TRUE(workers.count(*record->threads.begin()) > 0);
}

TEST_F(PooledFixture, periodicTimerInsideThePoolFiresUntilStopped)
{
    TaskPool pool(2);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    worker->stopAfter = 5;
    worker->timer->start(1ms);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    ASSERT_TRUE(waitForTicks(*record, 5));
    std::this_thread::sleep_for(30ms);  // a stopped timer must stay quiet
    pool.waitForDone();

    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->ticks, 5);
    EXPECT_EQ(record->ticksOffOwner, 0);
    EXPECT_EQ(record->emitsOffOwner, 0);
}

TEST_F(PooledFixture, timerCreatedAndStartedInsideASlotFires)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    std::promise<bool> fired;
    std::atomic<bool> setOnce{false};
    worker->onValueHook = [&](Worker& self, int) {
        auto* timer = new Timer(&self);
        timer->setSingleShot(true);
        timer->timeout.connect(NodePtr<Worker>(&self), [&](const Worker& owner) {
            if (! setOnce.exchange(true)) {
                fired.set_value(owner.ownerExecutor()->isCurrent());
            }
        });
        timer->start(2ms);
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    auto future = fired.get_future();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(future.get());
    pool.waitForDone();
}

TEST_F(PooledFixture, staticSingleShotWithAPooledParentFiresAndCleansUp)
{
    TaskPool pool(2);
    auto* worker = new Worker();
    std::atomic<int> fired{0};
    std::atomic<std::size_t> childrenAfter{99};
    worker->onValueHook = [&](Worker& self, int value) {
        if (value == 0) {
            Timer::singleShot(2ms, [&fired]() { fired.fetch_add(1); }, &self);
        } else {
            childrenAfter.store(self.childrenCount());
        }
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    ASSERT_TRUE(waitUntil([&]() { return fired.load() == 1; }));
    pool.waitForDone();
    input.emit(1);
    pool.waitForDone();

    EXPECT_EQ(fired.load(), 1);
    EXPECT_EQ(childrenAfter.load(), 0u);  // the single-shot timer deleted itself
}

TEST_F(PooledFixture, timerControlFromOutsideThePoolIsForwardedToTheStrand)
{
    TaskPool pool(2);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    // From main: the node lives in the pool, so these are posted to it.
    worker->timer->setSingleShot(true);
    worker->timer->setInterval(3ms);
    worker->timer->start();

    ASSERT_TRUE(waitForTicks(*record, 1));
    pool.waitForDone();

    worker->timer->start(50ms);
    worker->timer->stop();  // applied in order: the restart is cancelled
    std::this_thread::sleep_for(80ms);
    pool.waitForDone();

    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->ticks, 1);
    EXPECT_EQ(record->ticksOffOwner, 0);
    EXPECT_EQ(record->emitsOffOwner, 0);
}

TEST_F(PooledFixture, timerOfAPooledNodeFollowsItBackToAFixedThread)
{
    TaskPool pool(2);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    worker->timer->setSingleShot(true);
    worker->timer->start(20ms);

    ASSERT_TRUE(worker->moveToThread(std::this_thread::get_id()));  // posted to the strand
    pool.waitForDone();
    ASSERT_NE(worker->ownerEventLoop(), nullptr);

    ASSERT_TRUE(waitUntil([&]() {
        app->loopOnce();
        return ticksOf(*record) > 0;
    }));
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->ticks, 1);
    EXPECT_EQ(record->threads.count(std::this_thread::get_id()), 1u);
}

TEST_F(PooledFixture, destroyingAPooledNodeCancelsItsTimer)
{
    TaskPool pool(2);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    worker->timer->start(10ms);

    worker->deleteLater();
    pool.waitForDone();
    std::this_thread::sleep_for(40ms);
    pool.waitForDone();

    EXPECT_EQ(ticksOf(*record), 0);
}

TEST_F(PooledFixture, shutdownWithAPeriodicPooledTimerDoesNotHang)
{
    auto record = std::make_shared<TickingWorker::Record>();
    auto doneFlag = std::make_shared<std::atomic<bool>>(false);
    std::thread runner([record, doneFlag]() {
        TaskPool pool(2);
        auto* worker = new TickingWorker(record);
        worker->timer->start(1ms);
        if (worker->moveToTaskPool(&pool)) {
            waitForTicks(*record, 3);
        }
        pool.shutdown();  // stops the timers, then destroys the node
        doneFlag->store(true);
    });

    const bool finished = waitUntil([&]() { return doneFlag->load(); }, 5s);
    if (finished) {
        runner.join();
    } else {
        runner.detach();  // keep the suite alive; the assertion reports it
    }
    ASSERT_TRUE(finished) << "TaskPool::shutdown() hung with a periodic pooled timer";
    EXPECT_GE(ticksOf(*record), 3);
}

TEST_F(PooledFixture, stressManyPooledNodesWithPeriodicTimers)
{
    TaskPool pool(4);
    constexpr int kNodes = 32;
    std::vector<std::shared_ptr<TickingWorker::Record>> records;
    std::vector<TickingWorker*> workers;
    for (int i = 0; i < kNodes; ++i) {
        records.push_back(std::make_shared<TickingWorker::Record>());
        auto* worker = new TickingWorker(records.back());
        worker->stopAfter = 10;
        worker->timer->start(1ms);
        ASSERT_TRUE(worker->moveToTaskPool(&pool));
        workers.push_back(worker);
    }

    for (const auto& record : records) {
        ASSERT_TRUE(waitForTicks(*record, 10, 5s));
    }
    // Destroy half while the rest may still be finishing their last tick.
    for (int i = 0; i < kNodes; i += 2) {
        workers[i]->deleteLater();
    }
    pool.waitForDone();

    std::set<std::thread::id> allThreads;
    for (const auto& record : records) {
        std::lock_guard<std::mutex> lock(record->mutex);
        EXPECT_EQ(record->ticks, 10);
        EXPECT_EQ(record->ticksOffOwner, 0);
        EXPECT_EQ(record->emitsOffOwner, 0);
        allThreads.insert(record->threads.begin(), record->threads.end());
    }
    EXPECT_GT(allThreads.size(), 1u);  // the ticks spread over several workers
}

TEST_F(PooledFixture, timeoutInFlightWhenThePooledNodeMigratesFiresOnlyOnceOnTheNewOwner)
{
    // The timeout is posted to the strand while a slot keeps the strand busy;
    // that slot then moves the node to main. The posted timeout must be
    // dropped (the timer no longer belongs to the strand) and the timer, which
    // main re-armed with the same, already passed deadline, fires once there.
    TaskPool pool(2);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    const std::thread::id mainId = std::this_thread::get_id();
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    Signal<int> input;
    input.connect(NodePtr<TickingWorker>(worker), [mainId](TickingWorker& self, int) {
        self.timer->setSingleShot(true);
        self.timer->start(1ms);
        std::this_thread::sleep_for(30ms);  // the deadline passes: timeout posted behind us
        self.moveToThread(mainId);          // inside the owner: applied now
    });
    input.emit(0);
    pool.waitForDone();
    ASSERT_NE(worker->ownerEventLoop(), nullptr);

    ASSERT_TRUE(waitUntil([&]() {
        app->loopOnce();
        return ticksOf(*record) > 0;
    }));
    for (int i = 0; i < 10; ++i) {
        app->loopOnce();
    }
    std::lock_guard<std::mutex> lock(record->mutex);
    EXPECT_EQ(record->ticks, 1);
    EXPECT_EQ(record->ticksOffOwner, 0);
    EXPECT_EQ(record->emitsOffOwner, 0);
    EXPECT_EQ(record->threads.count(mainId), 1u);
    EXPECT_EQ(record->threads.size(), 1u);
}

TEST_F(PooledFixture, aPooledTimerDueDuringShutdownDoesNotFire)
{
    TaskPool pool(1);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    std::promise<void> armed;

    Signal<int> input;
    input.connect(NodePtr<TickingWorker>(worker), [&armed](TickingWorker& self, int) {
        self.timer->setSingleShot(true);
        self.timer->start(20ms);
        armed.set_value();
        std::this_thread::sleep_for(60ms);  // the deadline passes while shutdown() waits
    });
    input.emit(0);
    armed.get_future().wait();
    pool.shutdown();

    EXPECT_EQ(ticksOf(*record), 0);
}

TEST_F(PooledFixture, stoppedOrDestroyedPooledTimersAreDisarmedAtOnce)
{
    TaskPool pool(2);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    EXPECT_EQ(pool.armedTimerCount(), 0u);

    worker->timer->start(1h);
    worker->timer->start(1h);  // restarting replaces the deadline, it does not add one
    pool.waitForDone();
    EXPECT_EQ(pool.armedTimerCount(), 1u);

    worker->timer->stop();
    pool.waitForDone();
    EXPECT_EQ(pool.armedTimerCount(), 0u);

    worker->timer->start(1h);
    pool.waitForDone();
    EXPECT_EQ(pool.armedTimerCount(), 1u);
    worker->deleteLater();
    pool.waitForDone();
    EXPECT_EQ(pool.armedTimerCount(), 0u);
}

TEST_F(PooledFixture, aTimerStartedInsideASlotWhileShutdownDrainsNeverFires)
{
    TaskPool pool(1);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));
    std::promise<void> running;

    Signal<int> input;
    input.connect(NodePtr<TickingWorker>(worker), [&running](TickingWorker& self, int) {
        running.set_value();
        std::this_thread::sleep_for(30ms);  // shutdown() has started meanwhile
        self.timer->setSingleShot(true);
        self.timer->start(1ms);  // the timer service is already stopped: ignored
        std::this_thread::sleep_for(20ms);
    });
    input.emit(0);
    running.get_future().wait();
    pool.shutdown();

    EXPECT_EQ(ticksOf(*record), 0);
}

TEST_F(PooledFixture, aTimeoutPostedForATimerDestroyedBeforeItRunsIsDropped)
{
    TaskPool pool(1);
    auto* worker = new Worker();
    std::atomic<int> fired{0};
    worker->onValueHook = [&fired](Worker& self, int) {
        auto* timer = new Timer(&self);
        timer->setSingleShot(true);
        timer->timeout.connect([&fired]() { fired.fetch_add(1); });
        timer->start(1ms);
        std::this_thread::sleep_for(20ms);  // the timeout is posted behind this slot
        delete timer;                       // ...and finds the timer gone
    };
    Signal<int> input;
    input.connect(NodePtr<Worker>(worker), &Worker::onValue);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    input.emit(0);
    pool.waitForDone();
    std::this_thread::sleep_for(10ms);
    pool.waitForDone();

    EXPECT_EQ(fired.load(), 0);
}

TEST_F(PooledFixture, aTimeoutPostedBeforeTheTimerWasRestartedIsIgnored)
{
    TaskPool pool(1);
    auto record = std::make_shared<TickingWorker::Record>();
    auto* worker = new TickingWorker(record);
    ASSERT_TRUE(worker->moveToTaskPool(&pool));

    Signal<int> input;
    input.connect(NodePtr<TickingWorker>(worker), [](TickingWorker& self, int) {
        self.timer->setSingleShot(true);
        self.timer->start(1ms);
        std::this_thread::sleep_for(20ms);  // the 1 ms timeout is posted behind us
        self.timer->start(1h);              // new generation: that timeout is stale
    });
    input.emit(0);
    pool.waitForDone();
    std::this_thread::sleep_for(10ms);
    pool.waitForDone();

    EXPECT_EQ(ticksOf(*record), 0);
    EXPECT_EQ(pool.armedTimerCount(), 1u);  // only the 1 h deadline is left
}

// ═════════════════════════════════════════════════════════════════════════════
// Phase 5: IoService edge cases (sockets are covered in SNFNetworkTests)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PooledFixture, ioServiceIgnoresInvalidUnknownAndLateRequests)
{
    TaskPool pool(1);
    detail::Strand* strand = detail::Strand::acquire(pool);
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);

    detail::IoService service;
    service.registerIO(strand, -1, EPOLLIN, [](std::uint32_t) {});  // negative fd
    service.modifyIO(fds[0], EPOLLIN);                              // unknown fd
    service.unregisterIO(fds[0]);                                    // unknown fd
    EXPECT_EQ(service.watchCount(), 0u);
    EXPECT_FALSE(service.hasIOWatch(fds[0]));

    service.stop();
    service.registerIO(strand, fds[0], EPOLLIN, [](std::uint32_t) {});  // after stop
    EXPECT_EQ(service.watchCount(), 0u);

    ::close(fds[0]);
    ::close(fds[1]);
    detail::Strand::release(strand);
}

TEST_F(PooledFixture, ioServiceDeliversReadinessAndSurvivesAClosedDescriptor)
{
    TaskPool pool(1);
    detail::Strand* strand = detail::Strand::acquire(pool);
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);

    detail::IoService service;
    std::atomic<int> calls{0};
    std::atomic<bool> insideStrand{false};
    service.registerIO(strand, fds[0], EPOLLIN, [&](std::uint32_t events) {
        insideStrand.store(strand->isCurrent() && (events & EPOLLIN) != 0);
        char byte = 0;
        (void)::read(fds[0], &byte, 1);
        calls.fetch_add(1);
    });
    ASSERT_EQ(::write(fds[1], "x", 1), 1);
    ASSERT_TRUE(waitUntil([&]() { return calls.load() == 1; }));
    pool.waitForDone();
    EXPECT_TRUE(insideStrand.load());

    // Closing the descriptor behind the service's back must not throw when
    // the watch is changed or removed afterwards.
    ::close(fds[0]);
    EXPECT_NO_THROW(service.modifyIO(fds[0], EPOLLIN | EPOLLOUT));
    EXPECT_NO_THROW(service.unregisterIO(fds[0]));
    EXPECT_EQ(service.watchCount(), 0u);

    service.stop();
    ::close(fds[1]);
    detail::Strand::release(strand);
}

// ── A move hook may wait for work it hands to the destination ──────────────
// The destination is held while the subtree moves; work posted by the moving
// thread itself must still run, or such a hook would wait forever.

namespace {

struct HookWaitResult {
    std::atomic<bool> ranInTarget{false};
    std::atomic<bool> timedOut{false};
};

void postToOwnerAndWait(Node& node, HookWaitResult& result)
{
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    Executor* target = node.ownerExecutor();  // already the destination
    target->post([&result, done, target]() {
        result.ranInTarget.store(target->isCurrent());
        done->set_value();
    });
    result.timedOut.store(finished.wait_for(1s) != std::future_status::ready);
}

class LoopHookWaiter final : public Node
{
public:
    explicit LoopHookWaiter(HookWaitResult& result) : Node(nullptr), m_result(result) {}
    void update() override {}

protected:
    void onMovedToThread(EventLoop* /*oldLoop*/) override { postToOwnerAndWait(*this, m_result); }

private:
    HookWaitResult& m_result;
};

class StrandHookWaiter final : public PooledNode
{
public:
    explicit StrandHookWaiter(HookWaitResult& result) : m_result(result) {}

protected:
    void onMovedToThread(EventLoop* /*oldLoop*/) override { postToOwnerAndWait(*this, m_result); }

private:
    HookWaitResult& m_result;
};

}  // namespace

TEST_F(PooledFixture, moveToThreadHookCanWaitForWorkPostedToTheTargetLoop)
{
    ThreadPool pool(1);
    HookWaitResult result;
    auto* node = new LoopHookWaiter(result);
    ASSERT_TRUE(node->moveToThread(pool.workerThreadIds().at(0)));

    EXPECT_FALSE(result.timedOut.load());
    EXPECT_TRUE(result.ranInTarget.load());
    node->deleteLater();
}

TEST_F(PooledFixture, moveToTaskPoolHookCanWaitForWorkPostedToTheStrand)
{
    TaskPool pool(1);
    HookWaitResult result;
    auto* node = new StrandHookWaiter(result);
    ASSERT_TRUE(node->moveToTaskPool(&pool));

    EXPECT_FALSE(result.timedOut.load());
    EXPECT_TRUE(result.ranInTarget.load());
    pool.waitForDone();
    node->deleteLater();
    pool.waitForDone();
}
