#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/EnqueuedAsyncTask.h"
#include "SNFCore/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

class LambdaTask final : public snf::AsyncTask
{
public:
    explicit LambdaTask(std::function<void()> body)
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

}  // namespace

TEST_F(ThreadPoolFixture, applicationOwnsGlobalThreadPool)
{
    ASSERT_NE(app->threadPool(), nullptr);
    EXPECT_EQ(snf::ThreadPool::globalInstance(), app->threadPool());
    EXPECT_GE(app->threadPool()->maxThreadCount(), 1U);
}

TEST(ThreadPoolTests, twoThreadPoolDoesNotRunMoreThanTwoTasksConcurrently)
{
    snf::ThreadPool pool(2);
    std::atomic<int> ran{0};
    std::atomic<int> running{0};
    std::atomic<int> maxRunning{0};
    std::mutex threadsMutex;
    std::set<std::thread::id> workerThreads;

    for (int i = 0; i < 40; ++i) {
        ASSERT_TRUE(pool.start(std::make_shared<LambdaTask>([&]() {
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
}

TEST(ThreadPoolTests, enqueuedTasksFanInToSingleExitOnBoundedThreadPool)
{
    snf::ThreadPool pool(2);
    snf::EnqueuedAsyncTask graph;
    std::atomic<int> completedLeaves{0};
    std::atomic<int> finalRuns{0};
    std::atomic<int> graphFinishedSignals{0};
    std::mutex threadsMutex;
    std::set<std::thread::id> workerThreads;
    std::vector<snf::EnqueuedAsyncTask::TaskId> leaves;

    for (int i = 0; i < 20; ++i) {
        const auto id = graph.addTask(std::make_shared<LambdaTask>([&]() {
            {
                std::lock_guard<std::mutex> lock(threadsMutex);
                workerThreads.insert(std::this_thread::get_id());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++completedLeaves;
        }));
        ASSERT_NE(id, snf::EnqueuedAsyncTask::invalidTaskId);
        leaves.push_back(id);
    }

    const auto exitId = graph.addTask(std::make_shared<LambdaTask>([&]() {
        {
            std::lock_guard<std::mutex> lock(threadsMutex);
            workerThreads.insert(std::this_thread::get_id());
        }
        EXPECT_EQ(completedLeaves.load(), 20);
        ++finalRuns;
    }));
    ASSERT_NE(exitId, snf::EnqueuedAsyncTask::invalidTaskId);

    for (const auto leaf : leaves) {
        ASSERT_TRUE(graph.addDependency(leaf, exitId));
    }

    graph.finished.connect([&]() { ++graphFinishedSignals; });

    ASSERT_TRUE(graph.hasValidSingleExit());
    ASSERT_TRUE(graph.start(&pool));
    graph.wait();
    pool.waitForDone();

    EXPECT_TRUE(graph.isFinished());
    EXPECT_EQ(graph.finishedTaskCount(), 21U);
    EXPECT_EQ(completedLeaves.load(), 20);
    EXPECT_EQ(finalRuns.load(), 1);
    EXPECT_EQ(graphFinishedSignals.load(), 1);
    EXPECT_LE(workerThreads.size(), 2U);
}

TEST(ThreadPoolTests, enqueuedTaskRejectsMultipleExitsAndCycles)
{
    snf::EnqueuedAsyncTask multipleExits;
    const auto first = multipleExits.addTask(std::make_shared<LambdaTask>([]() {}));
    const auto second = multipleExits.addTask(std::make_shared<LambdaTask>([]() {}));
    ASSERT_NE(first, snf::EnqueuedAsyncTask::invalidTaskId);
    ASSERT_NE(second, snf::EnqueuedAsyncTask::invalidTaskId);
    EXPECT_FALSE(multipleExits.hasValidSingleExit());

    snf::EnqueuedAsyncTask cycle;
    const auto a = cycle.addTask(std::make_shared<LambdaTask>([]() {}));
    const auto b = cycle.addTask(std::make_shared<LambdaTask>([]() {}));
    ASSERT_TRUE(cycle.addDependency(a, b));
    ASSERT_TRUE(cycle.addDependency(b, a));
    EXPECT_FALSE(cycle.hasValidSingleExit());
}
