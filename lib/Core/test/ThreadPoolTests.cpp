#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Runnable.h"
#include "SNFCore/ThreadPool.h"
#include "SNFCore/WorkerSelectionPolicy.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

class LambdaRunnable final : public snf::Runnable
{
public:
    explicit LambdaRunnable(std::function<void()> body)
        : m_body(std::move(body))
    {
    }

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

class ThreadPoolFixture : public ::testing::Test
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

class PickLastWorkerPolicy final : public snf::IWorkerSelectionPolicy
{
public:
    std::size_t selectWorkerIndex(const std::vector<snf::WorkerLoadSnapshot>& snapshots) const override
    {
        if (snapshots.empty()) {
            return snapshots.size();
        }
        return snapshots.size() - 1;
    }
};

}  // namespace

TEST_F(ThreadPoolFixture, applicationOwnsGlobalThreadPool)
{
    ASSERT_NE(app->threadPool(), nullptr);
    EXPECT_EQ(snf::ThreadPool::globalInstance(), app->threadPool());
    EXPECT_GE(app->threadPool()->maxThreadCount(), 1U);
}

TEST_F(ThreadPoolFixture, workersRegisterIdleEventLoops)
{
    snf::ThreadPool* pool = app->threadPool();
    ASSERT_NE(pool, nullptr);

    const std::vector<std::thread::id> workerThreadIds = pool->workerThreadIds();
    ASSERT_FALSE(workerThreadIds.empty());

    for (const std::thread::id workerThreadId : workerThreadIds) {
        snf::EventLoop* loop = app->getEventLoopByThreadId(workerThreadId);
        ASSERT_NE(loop, nullptr);
        EXPECT_EQ(loop->ownerThreadId(), workerThreadId);
        EXPECT_FALSE(loop->hasPendingWork());
    }
}

TEST_F(ThreadPoolFixture, customWorkerSelectionPolicyIsApplied)
{
    snf::ThreadPool* pool = app->threadPool();
    ASSERT_NE(pool, nullptr);

    const std::vector<std::thread::id> workerThreadIds = pool->workerThreadIds();
    ASSERT_FALSE(workerThreadIds.empty());

    pool->setWorkerSelectionPolicy(std::make_shared<PickLastWorkerPolicy>());
    EXPECT_EQ(pool->preferredWorkerThreadId(), workerThreadIds.back());
}

TEST(ThreadPoolTests, twoThreadPoolDoesNotRunMoreThanTwoTasksConcurrently)
{
    snf::ThreadPool pool(2);
    const std::vector<std::thread::id> poolThreadIds = pool.workerThreadIds();
    const std::set<std::thread::id> expectedWorkers(poolThreadIds.begin(), poolThreadIds.end());
    std::atomic<int> ran{0};
    std::atomic<int> running{0};
    std::atomic<int> maxRunning{0};
    std::mutex threadsMutex;
    std::set<std::thread::id> workerThreads;

    for (int i = 0; i < 40; ++i) {
        ASSERT_TRUE(pool.start(std::make_shared<LambdaRunnable>([&]() {
            {
                std::lock_guard<std::mutex> lock(threadsMutex);
                workerThreads.insert(std::this_thread::get_id());
            }

            const int currentRunning = ++running;
            int previousMax = maxRunning.load();
            while (currentRunning > previousMax
                   && ! maxRunning.compare_exchange_weak(previousMax, currentRunning)) {
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            --running;
            ++ran;
        })));
    }

    pool.waitForDone();

    EXPECT_EQ(ran.load(), 40);
    EXPECT_LE(maxRunning.load(), 2);
    EXPECT_LE(workerThreads.size(), 2U);
    for (const std::thread::id workerThread : workerThreads) {
        EXPECT_TRUE(expectedWorkers.count(workerThread) > 0);
    }
}

TEST(ThreadPoolTests, shutdownWithoutApplicationDoesNotTouchDestroyedWorkerLoops)
{
    // Without an Application each worker owns a standalone EventLoop that is
    // destroyed as soon as run() returns. shutdown() must not touch a worker
    // loop after the worker can observe the stop request (checked under
    // ThreadSanitizer / AddressSanitizer).
    for (int i = 0; i < 100; ++i) {
        snf::ThreadPool pool(4);
        ASSERT_EQ(pool.workerThreadIds().size(), 4U);
    }
}

TEST(WorkerSelectionPolicyTests, defaultPolicyPrefersFewerNodesOverAnIdleTimerOrSocket)
{
    // Worker 0 hosts a single node with an armed timer (or an I/O watch),
    // which makes hasPendingWork permanently true. Worker 1 hosts five plain
    // nodes and happens to have nothing queued right now.
    snf::WorkerLoadSnapshot withTimer;
    withTimer.hasPendingWork = true;
    withTimer.registeredNodesCount = 1;
    withTimer.activeTimerCount = 1;

    snf::WorkerLoadSnapshot manyNodes;
    manyNodes.hasPendingWork = false;
    manyNodes.registeredNodesCount = 5;

    const snf::DefaultWorkerSelectionPolicy policy;
    EXPECT_EQ(policy.selectWorkerIndex({withTimer, manyNodes}), 0u);
    EXPECT_EQ(policy.selectWorkerIndex({manyNodes, withTimer}), 1u);
}

TEST(WorkerSelectionPolicyTests, defaultPolicyBreaksNodeTiesWithPendingDeletesThenTimers)
{
    snf::WorkerLoadSnapshot a;
    a.registeredNodesCount = 2;
    a.pendingDeleteCount = 1;

    snf::WorkerLoadSnapshot b;
    b.registeredNodesCount = 2;
    b.activeTimerCount = 3;

    snf::WorkerLoadSnapshot c;
    c.registeredNodesCount = 2;
    c.activeTimerCount = 1;

    const snf::DefaultWorkerSelectionPolicy policy;
    EXPECT_EQ(policy.selectWorkerIndex({a, b, c}), 2u);
    EXPECT_EQ(policy.selectWorkerIndex({a, b}), 1u);
}

TEST(WorkerSelectionPolicyTests, defaultPolicyReturnsNoSelectionForNoWorkers)
{
    const snf::DefaultWorkerSelectionPolicy policy;
    EXPECT_EQ(policy.selectWorkerIndex({}), 0u);
}
