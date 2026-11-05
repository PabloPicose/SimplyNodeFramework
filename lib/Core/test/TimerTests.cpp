#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "SNFCore/Application.h"
#include "SNFCore/Connection.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/ThreadPool.h"
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

class PlainNode final : public Node
{
public:
    explicit PlainNode(Node* parent = nullptr) : Node(parent) {}

protected:
    void update() override {}
};

// Runs @p fn on @p loop's thread and waits for it to finish.
template <typename Fn>
bool runOnLoop(EventLoop* loop, Fn fn, std::chrono::milliseconds timeout = 1000ms)
{
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> future = done->get_future();
    loop->post([fn, done]() mutable {
        fn();
        done->set_value();
    });
    return future.wait_for(timeout) == std::future_status::ready;
}

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
        if (EventLoop* loop = timer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
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
    timer.timeout.connect([&]() {
        ++emitted;
        if (EventLoop* loop = timer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

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
        if (EventLoop* loop = app->getOrCreateCurrentThreadEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
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
    timer1.timeout.connect([&]() {
        executionOrder.push_back(1);
        if (EventLoop* loop = timer1.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

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
        if (EventLoop* loop = timer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
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

// ---- Migration of active timers (moveToThread) ----

TEST_F(TimerFixture, activeTimerFiresOnNewThreadAfterMigration)
{
    auto* timer = new Timer();
    EventLoop* oldLoop = timer->ownerEventLoop();

    auto fired = std::make_shared<std::promise<std::thread::id>>();
    std::future<std::thread::id> firedFuture = fired->get_future();
    timer->timeout.connect([fired]() { fired->set_value(std::this_thread::get_id()); });

    timer->setSingleShot(true);
    timer->start(30ms);
    ASSERT_TRUE(timer->moveToThreadPool());

    EventLoop* newLoop = timer->ownerEventLoop();
    ASSERT_NE(newLoop, oldLoop);

    // The pending timeout must have moved with the timer.
    EXPECT_EQ(oldLoop->activeTimerCount(), 0u);

    ASSERT_EQ(firedFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(firedFuture.get(), newLoop->ownerThreadId());

    ASSERT_TRUE(runOnLoop(newLoop, [timer]() { delete timer; }));
}

TEST_F(TimerFixture, repeatingTimerKeepsFiringOnNewThreadAfterMigration)
{
    auto* timer = new Timer();
    EventLoop* oldLoop = timer->ownerEventLoop();

    std::atomic<int> firedOnNewThread{0};
    std::atomic<int> firedElsewhere{0};
    std::atomic<std::thread::id> expectedThread{};
    timer->timeout.connect([&]() {
        if (std::this_thread::get_id() == expectedThread.load()) {
            ++firedOnNewThread;
        } else {
            ++firedElsewhere;
        }
    });

    timer->start(10ms);
    ASSERT_TRUE(timer->moveToThreadPool());
    EventLoop* newLoop = timer->ownerEventLoop();
    expectedThread.store(newLoop->ownerThreadId());

    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (firedOnNewThread.load() < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }

    // Give the old loop a chance to fire anything it might still hold.
    oldLoop->runPendingWork();

    EXPECT_GE(firedOnNewThread.load(), 3);
    EXPECT_EQ(firedElsewhere.load(), 0);

    ASSERT_TRUE(runOnLoop(newLoop, [timer]() { delete timer; }));
}

TEST_F(TimerFixture, stopAfterMigrationPreventsTimeoutOnOldThread)
{
    auto* timer = new Timer();
    EventLoop* oldLoop = timer->ownerEventLoop();

    std::atomic<int> timeoutCount{0};
    timer->timeout.connect([&]() { ++timeoutCount; });

    timer->start(20ms);
    ASSERT_TRUE(timer->moveToThreadPool());
    EventLoop* newLoop = timer->ownerEventLoop();

    // Stop on the new owner thread before the first deadline.
    ASSERT_TRUE(runOnLoop(newLoop, [timer]() { timer->stop(); }));

    std::this_thread::sleep_for(60ms);
    oldLoop->runPendingWork();

    EXPECT_EQ(timeoutCount.load(), 0);
    EXPECT_EQ(oldLoop->activeTimerCount(), 0u);
    EXPECT_EQ(newLoop->activeTimerCount(), 0u);

    ASSERT_TRUE(runOnLoop(newLoop, [timer]() { delete timer; }));
}

TEST_F(TimerFixture, destroyingMigratedTimerLeavesNoEntryInOldLoop)
{
    auto* timer = new Timer();
    EventLoop* oldLoop = timer->ownerEventLoop();

    timer->start(1s);
    ASSERT_TRUE(timer->moveToThreadPool());
    EventLoop* newLoop = timer->ownerEventLoop();

    ASSERT_TRUE(runOnLoop(newLoop, [timer]() { delete timer; }));

    // A leftover entry here would hold a dangling Timer*.
    EXPECT_EQ(oldLoop->activeTimerCount(), 0u);
    EXPECT_EQ(newLoop->activeTimerCount(), 0u);
}

TEST_F(TimerFixture, childTimerMigratesWithParentNode)
{
    auto* parent = new PlainNode();
    auto* timer = new Timer(parent);
    EventLoop* oldLoop = parent->ownerEventLoop();

    auto fired = std::make_shared<std::promise<std::thread::id>>();
    std::future<std::thread::id> firedFuture = fired->get_future();
    timer->timeout.connect([fired]() { fired->set_value(std::this_thread::get_id()); });

    timer->setSingleShot(true);
    timer->start(30ms);
    ASSERT_TRUE(parent->moveToThreadPool());

    EventLoop* newLoop = parent->ownerEventLoop();
    EXPECT_EQ(timer->ownerEventLoop(), newLoop);
    EXPECT_EQ(oldLoop->activeTimerCount(), 0u);

    ASSERT_EQ(firedFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(firedFuture.get(), newLoop->ownerThreadId());

    ASSERT_TRUE(runOnLoop(newLoop, [parent]() { delete parent; }));
}
