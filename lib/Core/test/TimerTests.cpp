#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "SNFCore/Application.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/Timer.h"

using namespace snf;
using namespace std::chrono_literals;

namespace {

class TimerFixture : public ::testing::Test {
 public:
  void SetUp() override { app = new Application(0, nullptr); }

  void TearDown() override {
    delete app;
    app = nullptr;
  }

  Application* app = nullptr;
};

class TimeoutReceiver final : public Node {
 public:
  explicit TimeoutReceiver(Node* parent = nullptr) : Node(parent) {}

  void onTimeout() {
    ++invocations;
    callbackThread = std::this_thread::get_id();
    if (EventLoop* loop = ownerEventLoop()) {
      loop->post([loop]() { loop->stop(); });
    }
  }

  void onValue(int value) {
    receivedValue = value;
    callbackThread = std::this_thread::get_id();
    if (EventLoop* loop = ownerEventLoop()) {
      loop->post([loop]() { loop->stop(); });
    }
  }

  int invocations = 0;
  int receivedValue = 0;
  std::thread::id callbackThread;

 protected:
  void update() override {}
};

} // namespace

TEST_F(TimerFixture, singleShotTimerFiresOnOwnerThread) {
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

TEST_F(TimerFixture, repeatingTimerCanBeStoppedFromTimeout) {
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

TEST_F(TimerFixture, disconnectedTimeoutDoesNotInvokeSlot) {
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

TEST_F(TimerFixture, staticSingleShotExecutesOnCreationThread) {
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

TEST_F(TimerFixture, queuedConnectionRunsOnReceiverThread) {
  std::mutex mutex;
  std::condition_variable cv;
  bool ready = false;
  bool callbackExecuted = false;
  std::thread::id workerThreadId;
  NodePtr<TimeoutReceiver> receiver(nullptr);

  std::thread worker([&]() {
    EventLoop* loop = app->getOrCreateCurrentThreadEventLoop();
    ASSERT_NE(loop, nullptr);

    auto* node = new TimeoutReceiver();
    {
      std::lock_guard<std::mutex> lock(mutex);
      receiver = NodePtr<TimeoutReceiver>(node);
      workerThreadId = std::this_thread::get_id();
      ready = true;
    }
    cv.notify_one();

    // Create a simple timer to keep the loop alive while we wait for the callback
    Timer keepAliveTimer;
    keepAliveTimer.start(100);  // 100ms timeout, longer than we need

    loop->run();
  });

  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&]() { return ready; });
  }

  ASSERT_TRUE(receiver);

  Signal<int> signal;
  signal.connect(receiver, &TimeoutReceiver::onValue, ConnectionType::Queued);
  signal.emit(42);

  // Give the worker thread time to process the queued callback
  std::this_thread::sleep_for(50ms);

  worker.join();

  ASSERT_TRUE(receiver);
  EXPECT_EQ(receiver->receivedValue, 42);
  EXPECT_EQ(receiver->callbackThread, workerThreadId);
}

TEST_F(TimerFixture, multipleTimersFireInCorrectOrder) {
  std::vector<int> executionOrder;

  Timer timer1, timer2, timer3;

  // Timer 1: 30ms (should fire last)
  timer1.setSingleShot(true);
  timer1.timeout.connect([&]() {
    executionOrder.push_back(1);
  });

  // Timer 2: 10ms (should fire first)
  timer2.setSingleShot(true);
  timer2.timeout.connect([&]() {
    executionOrder.push_back(2);
  });

  // Timer 3: 20ms (should fire second)
  timer3.setSingleShot(true);
  timer3.timeout.connect([&]() {
    executionOrder.push_back(3);
  });

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