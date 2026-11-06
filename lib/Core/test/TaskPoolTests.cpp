#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/Runnable.h"
#include "SNFCore/TaskPool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

class LambdaRunnable final : public snf::Runnable
{
public:
    explicit LambdaRunnable(std::function<void()> body) : m_body(std::move(body)) {}

protected:
    void run() override
    {
        if (m_body) {
            m_body();
        }
    }

private:
    std::function<void()> m_body;
};

class PlainNode final : public snf::Node
{
public:
    explicit PlainNode(Node* parent = nullptr) : Node(parent) {}

protected:
    void update() override {}
};

class TaskPoolFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new snf::Application(0, nullptr); }
    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

    snf::Application* app = nullptr;
};

}  // namespace

// ---- Basic execution ----

TEST(TaskPoolTests, runsRunnableOnAWorkerAndEmitsFinishedThere)
{
    snf::TaskPool pool(2);
    const std::vector<std::thread::id> workerIds = pool.workerThreadIds();
    ASSERT_EQ(workerIds.size(), 2u);
    const std::set<std::thread::id> workers(workerIds.begin(), workerIds.end());

    std::mutex mutex;
    std::thread::id runThread;
    std::thread::id finishedThread;

    auto runnable = std::make_shared<LambdaRunnable>([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        runThread = std::this_thread::get_id();
    });
    runnable->finished.connect([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        finishedThread = std::this_thread::get_id();
    });

    ASSERT_TRUE(pool.start(runnable));
    pool.waitForDone();

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_TRUE(workers.count(runThread) > 0);
    EXPECT_EQ(finishedThread, runThread);
}

TEST(TaskPoolTests, runsPlainTasksAndWaitForDoneWaitsForAllOfThem)
{
    snf::TaskPool pool(3);
    std::atomic<int> ran{0};

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(pool.start([&ran]() { ran.fetch_add(1); }));
    }
    pool.waitForDone();

    EXPECT_EQ(ran.load(), 100);
    EXPECT_EQ(pool.queuedTaskCount(), 0u);
    EXPECT_EQ(pool.activeTaskCount(), 0u);
}

TEST(TaskPoolTests, waitForDoneReturnsImmediatelyWhenIdle)
{
    snf::TaskPool pool(1);
    pool.waitForDone();
    SUCCEED();
}

TEST(TaskPoolTests, rejectsNullRunnableAndEmptyTask)
{
    snf::TaskPool pool(1);
    EXPECT_FALSE(pool.start(std::shared_ptr<snf::Runnable>()));
    EXPECT_FALSE(pool.start(std::function<void()>()));
}

TEST(TaskPoolTests, defaultThreadCountIsAtLeastOne)
{
    EXPECT_GE(snf::TaskPool::defaultThreadCount(), 1u);
    snf::TaskPool pool;
    EXPECT_EQ(pool.maxThreadCount(), snf::TaskPool::defaultThreadCount());
}

// ---- Scheduling ----

TEST(TaskPoolTests, neverRunsMoreTasksAtOnceThanItHasWorkers)
{
    snf::TaskPool pool(2);
    std::atomic<int> running{0};
    std::atomic<int> maxRunning{0};

    for (int i = 0; i < 40; ++i) {
        ASSERT_TRUE(pool.start([&]() {
            const int now = ++running;
            int previous = maxRunning.load();
            while (now > previous && ! maxRunning.compare_exchange_weak(previous, now)) {
            }
            std::this_thread::sleep_for(1ms);
            --running;
        }));
    }
    pool.waitForDone();

    EXPECT_LE(maxRunning.load(), 2);
}

TEST(TaskPoolTests, idleWorkerTakesQueuedTasksWhileAnotherWorkerIsBlocked)
{
    // The point of the shared queue: a long task on one worker must not
    // hold back tasks queued after it while another worker is free.
    snf::TaskPool pool(2);

    std::promise<void> blockerStarted;
    std::promise<void> releaseBlocker;
    std::shared_future<void> release = releaseBlocker.get_future().share();
    ASSERT_TRUE(pool.start([&blockerStarted, release]() {
        blockerStarted.set_value();
        release.wait();
    }));
    blockerStarted.get_future().wait();

    std::atomic<int> done{0};
    std::promise<void> allDone;
    constexpr int kTasks = 10;
    for (int i = 0; i < kTasks; ++i) {
        ASSERT_TRUE(pool.start([&]() {
            if (done.fetch_add(1) + 1 == kTasks) {
                allDone.set_value();
            }
        }));
    }

    const bool finishedWhileBlocked = allDone.get_future().wait_for(1s) == std::future_status::ready;
    releaseBlocker.set_value();
    pool.waitForDone();

    EXPECT_TRUE(finishedWhileBlocked);
    EXPECT_EQ(done.load(), kTasks);
}

TEST(TaskPoolTests, acceptsTasksFromManyThreadsConcurrently)
{
    snf::TaskPool pool(4);
    std::atomic<int> ran{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < 8; ++p) {
        producers.emplace_back([&]() {
            for (int i = 0; i < 200; ++i) {
                EXPECT_TRUE(pool.start([&ran]() { ran.fetch_add(1); }));
            }
        });
    }
    for (std::thread& producer : producers) {
        producer.join();
    }
    pool.waitForDone();

    EXPECT_EQ(ran.load(), 8 * 200);
}

TEST(TaskPoolTests, tasksCanStartMoreTasksOnTheSamePool)
{
    snf::TaskPool pool(2);
    std::atomic<int> ran{0};

    ASSERT_TRUE(pool.start([&]() {
        ran.fetch_add(1);
        for (int i = 0; i < 5; ++i) {
            EXPECT_TRUE(pool.start([&ran]() { ran.fetch_add(1); }));
        }
    }));
    pool.waitForDone();

    EXPECT_EQ(ran.load(), 6);
}

// ---- Errors ----

TEST(TaskPoolTests, exceptionFromPlainTaskDoesNotStopTheWorker)
{
    snf::TaskPool pool(1);
    std::atomic<bool> laterTaskRan{false};

    ASSERT_TRUE(pool.start([]() { throw std::runtime_error("boom"); }));
    ASSERT_TRUE(pool.start([&]() { laterTaskRan.store(true); }));
    pool.waitForDone();

    EXPECT_TRUE(laterTaskRan.load());
}

TEST(TaskPoolTests, exceptionFromRunnableIsStoredInTheRunnable)
{
    snf::TaskPool pool(1);
    auto runnable = std::make_shared<LambdaRunnable>([]() { throw std::runtime_error("boom"); });

    ASSERT_TRUE(pool.start(runnable));
    pool.waitForDone();

    EXPECT_TRUE(runnable->hasException());
}

TEST(TaskPoolTests, waitForDoneAndShutdownFromOwnWorkerThrowInsteadOfDeadlocking)
{
    snf::TaskPool pool(1);
    std::atomic<bool> waitThrew{false};
    std::atomic<bool> shutdownThrew{false};

    ASSERT_TRUE(pool.start([&]() {
        try {
            pool.waitForDone();
        } catch (const std::logic_error&) {
            waitThrew.store(true);
        }
        try {
            pool.shutdown();
        } catch (const std::logic_error&) {
            shutdownThrew.store(true);
        }
    }));
    pool.waitForDone();

    EXPECT_TRUE(waitThrew.load());
    EXPECT_TRUE(shutdownThrew.load());
}

// ---- Shutdown ----

TEST(TaskPoolTests, shutdownRunsQueuedTasksThenRejectsNewOnes)
{
    snf::TaskPool pool(1);
    std::atomic<int> ran{0};

    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    ASSERT_TRUE(pool.start([released]() { released.wait(); }));
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(pool.start([&ran]() { ran.fetch_add(1); }));
    }

    std::thread releaser([&]() {
        std::this_thread::sleep_for(10ms);
        release.set_value();
    });
    pool.shutdown();
    releaser.join();

    EXPECT_EQ(ran.load(), 5);
    EXPECT_FALSE(pool.start([]() {}));
    EXPECT_EQ(pool.maxThreadCount(), 0u);
    pool.shutdown();  // idempotent
}

// ---- Application integration ----

TEST(TaskPoolTests, globalInstanceIsNullWithoutApplication)
{
    EXPECT_EQ(snf::TaskPool::globalInstance(), nullptr);
}

TEST_F(TaskPoolFixture, applicationOwnsGlobalTaskPool)
{
    ASSERT_NE(app->taskPool(), nullptr);
    EXPECT_EQ(snf::TaskPool::globalInstance(), app->taskPool());
    EXPECT_GE(app->taskPool()->maxThreadCount(), 1u);
}

TEST_F(TaskPoolFixture, workersHaveNoEventLoop)
{
    snf::TaskPool* pool = app->taskPool();
    const std::size_t loopsBefore = app->getEventLoopCount();

    std::atomic<bool> ran{false};
    ASSERT_TRUE(pool->start([&]() { ran.store(true); }));
    pool->waitForDone();

    EXPECT_TRUE(ran.load());
    EXPECT_EQ(app->getEventLoopCount(), loopsBefore);
    for (const std::thread::id& workerId : pool->workerThreadIds()) {
        EXPECT_EQ(app->getEventLoopByThreadId(workerId), nullptr);
    }
}

TEST_F(TaskPoolFixture, creatingANodeOnAWorkerThrows)
{
    // A worker has no EventLoop: the node would never get its queued signals
    // or deferred deletes. Fail loudly instead.
    snf::TaskPool* pool = app->taskPool();
    auto* parent = new PlainNode();
    std::atomic<int> rootThrew{0};
    std::atomic<int> childThrew{0};

    ASSERT_TRUE(pool->start([&]() {
        try {
            PlainNode root;
        } catch (const std::logic_error&) {
            rootThrew.fetch_add(1);
        }
        try {
            PlainNode child(parent);
        } catch (const std::logic_error&) {
            childThrew.fetch_add(1);
        }
    }));
    pool->waitForDone();

    EXPECT_EQ(rootThrew.load(), 1);
    EXPECT_EQ(childThrew.load(), 1);
    EXPECT_EQ(parent->childrenCount(), 0u);
    delete parent;
}

TEST_F(TaskPoolFixture, finishedSignalReachesNodeOnItsOwnThread)
{
    // finished is emitted on the worker; the default (Auto) connection to a
    // node on main queues it to main's loop.
    auto* receiver = new PlainNode();
    snf::EventLoop* mainLoop = receiver->ownerEventLoop();
    std::atomic<bool> delivered{false};
    std::thread::id deliveredOn;

    auto runnable = std::make_shared<LambdaRunnable>([]() {});
    runnable->finished.connect(snf::NodePtr<PlainNode>(receiver), [&](PlainNode&) {
        deliveredOn = std::this_thread::get_id();
        delivered.store(true);
    });

    ASSERT_TRUE(app->taskPool()->start(runnable));
    app->taskPool()->waitForDone();
    EXPECT_FALSE(delivered.load());

    mainLoop->runPendingWork();
    EXPECT_TRUE(delivered.load());
    EXPECT_EQ(deliveredOn, std::this_thread::get_id());

    delete receiver;
}

// ---- Remaining paths and interleavings ----

TEST(TaskPoolTests, nonStandardExceptionFromTaskDoesNotStopTheWorker)
{
    snf::TaskPool pool(1);
    std::atomic<bool> laterTaskRan{false};

    ASSERT_TRUE(pool.start([]() { throw 42; }));
    ASSERT_TRUE(pool.start([&]() { laterTaskRan.store(true); }));
    pool.waitForDone();

    EXPECT_TRUE(laterTaskRan.load());
}

TEST(TaskPoolTests, exceptionFromAFinishedSlotDoesNotStopTheWorker)
{
    // Runnable::execute() only guards run(); a throwing finished slot
    // reaches the worker, which must survive it.
    snf::TaskPool pool(1);
    auto runnable = std::make_shared<LambdaRunnable>([]() {});
    runnable->finished.connect([]() { throw std::runtime_error("slot"); });
    std::atomic<bool> laterTaskRan{false};

    ASSERT_TRUE(pool.start(runnable));
    ASSERT_TRUE(pool.start([&]() { laterTaskRan.store(true); }));
    pool.waitForDone();

    EXPECT_TRUE(laterTaskRan.load());
}

TEST(TaskPoolTests, destructorRunsQueuedTasksBeforeReturning)
{
    std::atomic<int> ran{0};
    {
        snf::TaskPool pool(1);
        for (int i = 0; i < 20; ++i) {
            ASSERT_TRUE(pool.start([&ran]() {
                std::this_thread::sleep_for(100us);
                ran.fetch_add(1);
            }));
        }
    }
    EXPECT_EQ(ran.load(), 20);
}

TEST(TaskPoolTests, concurrentShutdownCallsAllReturnOnlyAfterWorkersStopped)
{
    // Every shutdown() caller may rely on the pool being stopped when the
    // call returns, not only the first one.
    for (int iteration = 0; iteration < 50; ++iteration) {
        snf::TaskPool pool(2);
        std::atomic<bool> taskFinished{false};
        std::promise<void> taskStarted;
        ASSERT_TRUE(pool.start([&]() {
            taskStarted.set_value();
            std::this_thread::sleep_for(2ms);
            taskFinished.store(true);
        }));
        taskStarted.get_future().wait();

        std::atomic<int> sawUnfinished{0};
        std::vector<std::thread> callers;
        for (int i = 0; i < 3; ++i) {
            callers.emplace_back([&]() {
                pool.shutdown();
                if (! taskFinished.load()) {
                    sawUnfinished.fetch_add(1);
                }
            });
        }
        for (std::thread& caller : callers) {
            caller.join();
        }
        ASSERT_EQ(sawUnfinished.load(), 0) << "iteration " << iteration;
    }
}

TEST(TaskPoolTests, taskQueuedWhileShutdownDrainsIsRejected)
{
    snf::TaskPool pool(1);
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<int> startResult{-1};

    ASSERT_TRUE(pool.start([&, released]() {
        released.wait();
        startResult.store(pool.start([]() {}) ? 1 : 0);
    }));

    std::thread releaser([&]() {
        // Give shutdown() time to begin; whatever the timing, the start()
        // below happens after shutdown() was requested or the test is moot.
        std::this_thread::sleep_for(10ms);
        release.set_value();
    });
    pool.shutdown();
    releaser.join();

    EXPECT_EQ(startResult.load(), 0);
}

TEST(TaskPoolTests, waitForDoneAfterShutdownReturnsImmediately)
{
    snf::TaskPool pool(1);
    pool.shutdown();
    pool.waitForDone();
    SUCCEED();
}

TEST(TaskPoolTests, aTaskMayWaitForAnotherPool)
{
    snf::TaskPool inner(1);
    snf::TaskPool outer(1);
    std::atomic<bool> innerRan{false};
    std::atomic<bool> outerSawInnerDone{false};

    ASSERT_TRUE(outer.start([&]() {
        EXPECT_TRUE(inner.start([&]() { innerRan.store(true); }));
        inner.waitForDone();  // not its own pool: allowed
        outerSawInnerDone.store(innerRan.load());
    }));
    outer.waitForDone();

    EXPECT_TRUE(outerSawInnerDone.load());
}

TEST(TaskPoolTests, isWorkerThreadIsTrueOnlyOnWorkers)
{
    EXPECT_FALSE(snf::TaskPool::isWorkerThread());

    snf::TaskPool pool(1);
    std::atomic<bool> onWorker{false};
    ASSERT_TRUE(pool.start([&]() { onWorker.store(snf::TaskPool::isWorkerThread()); }));
    pool.waitForDone();

    EXPECT_TRUE(onWorker.load());
    EXPECT_FALSE(snf::TaskPool::isWorkerThread());
}
