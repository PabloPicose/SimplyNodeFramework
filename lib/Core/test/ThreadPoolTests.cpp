#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/Runnable.h"
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
