#include <gtest/gtest.h>

#include "SNFCore/Runnable.h"
#include "SNFCore/ThreadPool.h"

#include <chrono>
#include <condition_variable>
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

}  // namespace

TEST(RunnableTests, executesOnThreadPoolWorkersAndEmitsFinished)
{
    snf::ThreadPool pool(2);
    const std::vector<std::thread::id> workerIds = pool.workerThreadIds();
    ASSERT_EQ(workerIds.size(), 2U);
    const std::set<std::thread::id> expectedWorkers(workerIds.begin(), workerIds.end());

    std::mutex stateMutex;
    std::condition_variable stateChanged;
    std::thread::id runThread;
    std::thread::id finishedThread;
    bool finishedObserved = false;

    auto runnable = std::make_shared<LambdaRunnable>([&]() {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            runThread = std::this_thread::get_id();
        }
        stateChanged.notify_all();
    });

    runnable->finished.connect([&]() {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            finishedThread = std::this_thread::get_id();
            finishedObserved = true;
        }
        stateChanged.notify_all();
    });

    ASSERT_TRUE(pool.start(runnable));

    {
        std::unique_lock<std::mutex> lock(stateMutex);
        ASSERT_TRUE(stateChanged.wait_for(lock, std::chrono::seconds(1), [&]() { return finishedObserved; }));
    }

    pool.waitForDone();

    EXPECT_NE(runThread, std::thread::id{});
    EXPECT_NE(finishedThread, std::thread::id{});
    EXPECT_EQ(runThread, finishedThread);
    EXPECT_TRUE(expectedWorkers.count(runThread) > 0);
    EXPECT_TRUE(expectedWorkers.count(finishedThread) > 0);
}
