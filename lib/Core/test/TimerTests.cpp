#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "SNFCore/Application.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Timer.h"

using namespace snf;
using namespace std::chrono_literals;

namespace {

class TimerFixture : public ::testing::Test
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

}  // namespace

TEST_F(TimerFixture, singleShotTimerFiresOnOwnerThread)
{
    Timer timer;
    timer.setSingleShot(true);

    int timeoutCount = 0;
    std::thread::id callbackThread;

    timer.timeout.connect([&]() {
        ++timeoutCount;
        callbackThread = std::this_thread::get_id();
    });

    timer.start(15);
    app->run();

    EXPECT_EQ(timeoutCount, 1);
    EXPECT_EQ(callbackThread, timer.ownerThreadId());
    EXPECT_FALSE(timer.isActive());
}

TEST_F(TimerFixture, repeatingTimerCanBeStoppedFromTimeout)
{
    Timer timer;

    int timeoutCount = 0;
    timer.timeout.connect([&]() {
        ++timeoutCount;
        if (timeoutCount >= 3) {
            timer.stop();
        }
    });

    // Create a shutdown timer to stop the loop after enough time has passed
    Timer shutdownTimer;
    shutdownTimer.setSingleShot(true);
    shutdownTimer.timeout.connect([&]() {
        if (EventLoop* loop = shutdownTimer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    shutdownTimer.start(50);

    timer.start(5);
    app->run();

    EXPECT_EQ(timeoutCount, 3);
    EXPECT_FALSE(timer.isActive());
}

TEST_F(TimerFixture, disconnectedTimeoutDoesNotInvokeSlot)
{
    Timer timer;
    timer.setSingleShot(true);

    int disconnectedCount = 0;
    Connection connection = timer.timeout.connect([&]() { ++disconnectedCount; });
    int emitted = 0;
    timer.timeout.connect([&]() { ++emitted; });

    connection.disconnect();
    EXPECT_FALSE(connection.connected());

    timer.start(10);
    app->run();

    EXPECT_EQ(disconnectedCount, 0);
    EXPECT_EQ(emitted, 1);
}

TEST_F(TimerFixture, staticSingleShotExecutesOnCreationThread)
{
    int timeoutCount = 0;
    std::thread::id callbackThread;

    Timer::singleShot(10ms, [&]() {
        ++timeoutCount;
        callbackThread = std::this_thread::get_id();
    });

    app->run();

    EXPECT_EQ(timeoutCount, 1);
    EXPECT_EQ(callbackThread, std::this_thread::get_id());
}

TEST_F(TimerFixture, multipleTimersFireInCorrectOrder)
{
    std::vector<int> executionOrder;

    Timer timer1, timer2, timer3;

    // Timer 1: 30ms (should fire last)
    timer1.setSingleShot(true);
    timer1.timeout.connect([&]() { executionOrder.push_back(1); });

    // Timer 2: 10ms (should fire first)
    timer2.setSingleShot(true);
    timer2.timeout.connect([&]() { executionOrder.push_back(2); });

    // Timer 3: 20ms (should fire second)
    timer3.setSingleShot(true);
    timer3.timeout.connect([&]() { executionOrder.push_back(3); });

    // Start them in creation order, not execution order
    timer1.start(30);
    timer2.start(10);
    timer3.start(20);

    app->run();

    ASSERT_EQ(executionOrder.size(), 3);
    EXPECT_EQ(executionOrder[0], 2);  // 10ms fires first
    EXPECT_EQ(executionOrder[1], 3);  // 20ms fires second
    EXPECT_EQ(executionOrder[2], 1);  // 30ms fires last
}

TEST_F(TimerFixture, stopBeforeFirstFirePreventsTimeout)
{
    int timeoutCount = 0;

    Timer timer;
    timer.setSingleShot(true);
    timer.timeout.connect([&]() { ++timeoutCount; });

    Timer stopTimer;
    stopTimer.setSingleShot(true);
    stopTimer.timeout.connect([&]() { timer.stop(); });

    Timer shutdownTimer;
    shutdownTimer.setSingleShot(true);
    shutdownTimer.timeout.connect([&]() {
        if (EventLoop* loop = shutdownTimer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    timer.start(50ms);
    stopTimer.start(10ms);
    shutdownTimer.start(80ms);

    app->run();

    EXPECT_EQ(timeoutCount, 0);
    EXPECT_FALSE(timer.isActive());
}

TEST_F(TimerFixture, startAndStopFromForeignThreadAreSafe)
{
    int timeoutCount = 0;

    Timer timer;
    timer.setSingleShot(true);
    timer.timeout.connect([&]() { ++timeoutCount; });

    Timer shutdownTimer;
    shutdownTimer.setSingleShot(true);
    shutdownTimer.timeout.connect([&]() {
        if (EventLoop* loop = shutdownTimer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    shutdownTimer.start(120ms);

    std::thread worker([&]() {
        timer.start(60ms);
        timer.stop();
    });
    worker.join();

    app->run();

    EXPECT_EQ(timeoutCount, 0);
    EXPECT_FALSE(timer.isActive());
}

TEST_F(TimerFixture, setIntervalWhileActiveReschedulesNextTimeout)
{
    std::vector<std::chrono::steady_clock::time_point> fireTimes;

    Timer timer;
    timer.timeout.connect([&]() {
        fireTimes.push_back(std::chrono::steady_clock::now());
        if (fireTimes.size() == 1) {
            timer.setInterval(40ms);
            return;
        }

        timer.stop();
        if (EventLoop* loop = timer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    timer.start(10ms);
    app->run();

    ASSERT_GE(fireTimes.size(), 2);
    const auto secondGap = std::chrono::duration_cast<std::chrono::milliseconds>(fireTimes[1] - fireTimes[0]);
    EXPECT_GE(secondGap, 30ms);
}

TEST_F(TimerFixture, restartSuppressesStaleScheduledTimeout)
{
    int timeoutCount = 0;

    Timer timer;
    timer.setSingleShot(true);
    timer.timeout.connect([&]() { ++timeoutCount; });

    Timer restartTimer;
    restartTimer.setSingleShot(true);
    restartTimer.timeout.connect([&]() {
        timer.stop();
        timer.start(220ms);
    });

    Timer shutdownTimer;
    shutdownTimer.setSingleShot(true);
    shutdownTimer.timeout.connect([&]() {
        timer.stop();
        if (EventLoop* loop = shutdownTimer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    timer.start(100ms);
    restartTimer.start(20ms);
    shutdownTimer.start(150ms);

    app->run();

    EXPECT_EQ(timeoutCount, 0);
    EXPECT_FALSE(timer.isActive());
}

TEST_F(TimerFixture, startWithoutArgumentsUsesConfiguredInterval)
{
    Timer timer;

    EXPECT_FALSE(timer.isSingleShot());

    timer.setInterval(15ms);
    EXPECT_EQ(timer.interval(), 15ms);

    timer.setSingleShot(true);
    EXPECT_TRUE(timer.isSingleShot());

    int timeoutCount = 0;
    std::thread::id callbackThread;

    timer.timeout.connect([&]() {
        ++timeoutCount;
        callbackThread = std::this_thread::get_id();
    });

    timer.start();
    app->run();

    EXPECT_EQ(timeoutCount, 1);
    EXPECT_EQ(callbackThread, timer.ownerThreadId());
    EXPECT_FALSE(timer.isActive());
}

TEST_F(TimerFixture, startWithoutArgumentsFromForeignThreadUsesConfiguredInterval)
{
    Timer timer;

    timer.setInterval(20ms);
    EXPECT_EQ(timer.interval(), 20ms);

    timer.setSingleShot(true);
    EXPECT_TRUE(timer.isSingleShot());

    int timeoutCount = 0;
    std::thread::id callbackThread;

    timer.timeout.connect([&]() {
        ++timeoutCount;
        callbackThread = std::this_thread::get_id();
    });

    Timer shutdownTimer;
    shutdownTimer.setSingleShot(true);
    shutdownTimer.timeout.connect([&]() {
        if (EventLoop* loop = shutdownTimer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    shutdownTimer.start(100ms);

    std::thread worker([&]() { timer.start(); });
    worker.join();

    app->run();

    EXPECT_EQ(timeoutCount, 1);
    EXPECT_EQ(callbackThread, timer.ownerThreadId());
    EXPECT_FALSE(timer.isActive());
}