#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "SNFCore/Application.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/ThreadPool.h"
#include "SNFCore/Timer.h"

using namespace snf;
using namespace std::chrono_literals;

namespace {

class ConnectionFixture : public ::testing::Test
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

class ValueReceiver final : public Node
{
public:
    explicit ValueReceiver(int* externalInvocations = nullptr, Node* parent = nullptr)
        : Node(parent), m_externalInvocations(externalInvocations)
    {
    }

    void onValue(int value)
    {
        receivedValue = value;
        callbackThread = std::this_thread::get_id();
        ++invocations;
        if (m_externalInvocations) {
            ++(*m_externalInvocations);
        }
        if (EventLoop* loop = ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    }

    int receivedValue = 0;
    int invocations = 0;
    std::thread::id callbackThread;

protected:
    void update() override {}

private:
    int* m_externalInvocations = nullptr;
};

class OrderedReceiver final : public Node
{
public:
    explicit OrderedReceiver(int expectedCount, Node* parent = nullptr)
        : Node(parent), m_expectedCount(expectedCount)
    {
    }

    void onValue(int value)
    {
        receivedValues.push_back(value);
        if (static_cast<int>(receivedValues.size()) == m_expectedCount) {
            if (EventLoop* loop = ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    }

    std::vector<int> receivedValues;

protected:
    void update() override {}

private:
    int m_expectedCount = 0;
};

}  // namespace

TEST_F(ConnectionFixture, directConnectionRunsOnEmitterThread)
{
    Signal<int> signal;
    std::thread::id callbackThread;
    int received = 0;

    signal.connect([&](int value) {
        received = value;
        callbackThread = std::this_thread::get_id();
    });

    signal.emit(42);

    EXPECT_EQ(received, 42);
    EXPECT_EQ(callbackThread, std::this_thread::get_id());
}

TEST_F(ConnectionFixture, queuedConnectionRunsOnReceiverThread)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    std::thread::id workerThreadId;
    NodePtr<ValueReceiver> receiver(nullptr);

    std::thread worker([&]() {
        EventLoop* loop = app->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(loop, nullptr);

        auto* node = new ValueReceiver();
        {
            std::lock_guard<std::mutex> lock(mutex);
            receiver = NodePtr<ValueReceiver>(node);
            workerThreadId = std::this_thread::get_id();
            ready = true;
        }
        cv.notify_one();

        // Keep run() blocked until queued signal arrives.
        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(2s);

        loop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return ready; });
    }

    ASSERT_TRUE(receiver);

    Signal<int> signal;
    signal.connect(receiver, &ValueReceiver::onValue, ConnectionType::Queued);
    signal.emit(42);

    worker.join();

    ASSERT_TRUE(receiver);
    EXPECT_EQ(receiver->receivedValue, 42);
    EXPECT_EQ(receiver->callbackThread, workerThreadId);
    EXPECT_EQ(receiver->invocations, 1);
}

TEST_F(ConnectionFixture, disconnectPreventsFutureInvocations)
{
    Signal<int> signal;

    int first = 0;
    int second = 0;

    Connection firstConn = signal.connect([&](int value) { first += value; });
    signal.connect([&](int value) { second += value; });

    signal.emit(3);
    firstConn.disconnect();
    signal.emit(5);

    EXPECT_EQ(first, 3);
    EXPECT_EQ(second, 8);
    EXPECT_FALSE(firstConn.connected());
}

TEST_F(ConnectionFixture, queuedConnectionSkipsReceiverMarkedToDelete)
{
    int externalInvocations = 0;
    ValueReceiver* receiver = new ValueReceiver(&externalInvocations);
    ASSERT_NE(receiver, nullptr);

    Signal<int> signal;
    signal.connect(NodePtr<ValueReceiver>(receiver), &ValueReceiver::onValue, ConnectionType::Queued);

    receiver->deleteLater();
    signal.emit(11);
    app->loopOnce();

    EXPECT_EQ(externalInvocations, 0);
    NodePtr<ValueReceiver> receiverPtr(receiver);
    EXPECT_FALSE(receiverPtr);
}

TEST_F(ConnectionFixture, directConnectionDeliversMultipleEmitsInOrder)
{
    Signal<int> signal;
    std::vector<int> received;

    signal.connect([&](int value) { received.push_back(value); });

    signal.emit(1);
    signal.emit(2);
    signal.emit(3);
    signal.emit(4);
    signal.emit(5);

    EXPECT_EQ(received, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST_F(ConnectionFixture, queuedConnectionDeliversMultipleEmitsInOrder)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    NodePtr<OrderedReceiver> receiver(nullptr);

    std::thread worker([&]() {
        EventLoop* loop = app->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(loop, nullptr);

        auto* node = new OrderedReceiver(5);
        {
            std::lock_guard<std::mutex> lock(mutex);
            receiver = NodePtr<OrderedReceiver>(node);
            ready = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(2s);

        loop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return ready; });
    }

    ASSERT_TRUE(receiver);

    Signal<int> signal;
    signal.connect(receiver, &OrderedReceiver::onValue, ConnectionType::Queued);

    signal.emit(1);
    signal.emit(2);
    signal.emit(3);
    signal.emit(4);
    signal.emit(5);

    worker.join();

    ASSERT_TRUE(receiver);
    EXPECT_EQ(receiver->receivedValues, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST_F(ConnectionFixture, multipleSlotsAllowPartialDisconnect)
{
    Signal<int> signal;

    int left = 0;
    int right = 0;

    Connection leftConn = signal.connect([&](int value) { left += value; });
    signal.connect([&](int value) { right += value; });

    signal.emit(5);
    leftConn.disconnect();
    signal.emit(2);

    EXPECT_EQ(left, 5);
    EXPECT_EQ(right, 7);
}

TEST_F(ConnectionFixture, queuedConnectionFollowsReceiverAfterMoveToThread)
{
    // Phase 1: receiver lives on the main thread.
    // The Application constructor already creates the main-thread EventLoop.
    const std::thread::id mainThreadId = std::this_thread::get_id();

    auto* receiver = new ValueReceiver();
    NodePtr<ValueReceiver> receiverPtr(receiver);

    Signal<int> signal;
    signal.connect(receiverPtr, &ValueReceiver::onValue, ConnectionType::Queued);

    // The emit posts a task to the main EventLoop; app->run() drains it.
    // ValueReceiver::onValue posts loop->stop() so run() returns after the
    // first invocation.
    signal.emit(42);
    app->run();

    EXPECT_EQ(receiver->receivedValue, 42);
    EXPECT_EQ(receiver->callbackThread, mainThreadId);
    EXPECT_EQ(receiver->invocations, 1);

    // Phase 2: migrate receiver to a worker thread and re-emit.
    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady = false;
    std::thread::id workerThreadId;

    std::thread worker([&]() {
        EventLoop* workerLoop = app->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerThreadId = std::this_thread::get_id();
            workerReady = true;
        }
        cv.notify_one();

        // Safety net: stop the loop if the signal never arrives.
        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(2s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    // Called from the owner (main) thread → migration is synchronous.
    ASSERT_TRUE(receiver->moveToThread(workerThreadId));

    // Queued delivery now targets the worker EventLoop.
    signal.emit(99);

    worker.join();

    ASSERT_TRUE(receiverPtr);
    EXPECT_EQ(receiver->receivedValue, 99);
    EXPECT_EQ(receiver->callbackThread, workerThreadId);
    EXPECT_EQ(receiver->invocations, 2);
}

// ---- ConnectionType::Auto ----

TEST_F(ConnectionFixture, autoConnectionRunsSynchronouslyWhenEmittedOnReceiverThread)
{
    auto* receiver = new ValueReceiver();
    int received = 0;
    std::thread::id callbackThread;

    Signal<int> signal;
    signal.connect(
        NodePtr<ValueReceiver>(receiver),
        [&](ValueReceiver&, int value) {
            received = value;
            callbackThread = std::this_thread::get_id();
        },
        ConnectionType::Auto);

    signal.emit(42);

    // Delivered inside emit(), without running the event loop.
    EXPECT_EQ(received, 42);
    EXPECT_EQ(callbackThread, std::this_thread::get_id());

    delete receiver;
}

TEST_F(ConnectionFixture, autoConnectionQueuesWhenEmittedFromAnotherThread)
{
    auto* receiver = new ValueReceiver();
    EventLoop* mainLoop = receiver->ownerEventLoop();
    std::atomic<int> received{0};
    std::thread::id callbackThread;

    Signal<int> signal;
    signal.connect(
        NodePtr<ValueReceiver>(receiver),
        [&](ValueReceiver&, int value) {
            callbackThread = std::this_thread::get_id();
            received.store(value);
        },
        ConnectionType::Auto);

    std::thread emitter([&]() { signal.emit(42); });
    emitter.join();

    // Not run on the emitter thread: it waits on the receiver's loop.
    EXPECT_EQ(received.load(), 0);

    mainLoop->runPendingWork();

    EXPECT_EQ(received.load(), 42);
    EXPECT_EQ(callbackThread, std::this_thread::get_id());

    delete receiver;
}

TEST_F(ConnectionFixture, autoConnectionFollowsReceiverAfterMoveToThread)
{
    ThreadPool pool(1);
    const std::thread::id workerId = pool.workerThreadIds().at(0);
    EventLoop* workerLoop = app->getEventLoopByThreadId(workerId);
    ASSERT_NE(workerLoop, nullptr);

    auto* receiver = new ValueReceiver();
    ASSERT_TRUE(receiver->moveToThread(workerId));

    // Only touched on the worker thread, read after synchronising with it.
    int received = 0;
    std::vector<std::thread::id> callbackThreads;

    Signal<int> signal;
    signal.connect(
        NodePtr<ValueReceiver>(receiver),
        [&](ValueReceiver&, int value) {
            received = value;
            callbackThreads.push_back(std::this_thread::get_id());
        },
        ConnectionType::Auto);

    // From main the receiver is foreign: queued to the worker.
    signal.emit(1);

    // From the worker it is local: synchronous, inside emit().
    auto done = std::make_shared<std::promise<int>>();
    std::future<int> future = done->get_future();
    workerLoop->post([&signal, &received, done]() {
        signal.emit(2);
        done->set_value(received);
    });
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);

    EXPECT_EQ(future.get(), 2);
    // Both deliveries ran on the worker, the first one before this task (FIFO).
    EXPECT_EQ(callbackThreads, (std::vector<std::thread::id>{workerId, workerId}));

    auto deleted = std::make_shared<std::promise<void>>();
    std::future<void> deletedFuture = deleted->get_future();
    workerLoop->post([receiver, deleted]() {
        delete receiver;
        deleted->set_value();
    });
    ASSERT_EQ(deletedFuture.wait_for(1s), std::future_status::ready);
}

// ---- Default connection type and the remaining delivery contracts ----

TEST_F(ConnectionFixture, defaultConnectionWithReceiverQueuesWhenEmittedFromAnotherThread)
{
    auto* receiver = new ValueReceiver();
    EventLoop* mainLoop = receiver->ownerEventLoop();
    std::atomic<int> received{0};
    std::thread::id callbackThread;

    Signal<int> signal;
    // No ConnectionType: the default is Auto.
    signal.connect(NodePtr<ValueReceiver>(receiver), [&](ValueReceiver&, int value) {
        callbackThread = std::this_thread::get_id();
        received.store(value);
    });

    std::thread emitter([&]() { signal.emit(7); });
    emitter.join();
    EXPECT_EQ(received.load(), 0);

    mainLoop->runPendingWork();
    EXPECT_EQ(received.load(), 7);
    EXPECT_EQ(callbackThread, std::this_thread::get_id());

    delete receiver;
}

TEST_F(ConnectionFixture, explicitDirectConnectionRunsOnEmitterThreadEvenForForeignReceiver)
{
    auto* receiver = new ValueReceiver();
    std::thread::id callbackThread;
    std::thread::id emitterThread;

    Signal<int> signal;
    signal.connect(
        NodePtr<ValueReceiver>(receiver),
        [&](ValueReceiver&, int) { callbackThread = std::this_thread::get_id(); },
        ConnectionType::Direct);

    std::thread emitter([&]() {
        emitterThread = std::this_thread::get_id();
        signal.emit(1);
    });
    emitter.join();

    EXPECT_EQ(callbackThread, emitterThread);

    delete receiver;
}

TEST_F(ConnectionFixture, queuedConnectionDefersEvenWhenEmittedOnReceiverThread)
{
    auto* receiver = new ValueReceiver();
    EventLoop* mainLoop = receiver->ownerEventLoop();
    int received = 0;

    Signal<int> signal;
    signal.connect(
        NodePtr<ValueReceiver>(receiver), [&](ValueReceiver&, int value) { received = value; },
        ConnectionType::Queued);

    signal.emit(5);
    EXPECT_EQ(received, 0);  // never inside emit(), unlike Auto

    mainLoop->runPendingWork();
    EXPECT_EQ(received, 5);

    delete receiver;
}

TEST_F(ConnectionFixture, connectionWithoutReceiverRunsOnEmitterThreadFromAnyThread)
{
    Signal<int> signal;
    std::thread::id callbackThread;
    std::thread::id emitterThread;

    signal.connect([&](int) { callbackThread = std::this_thread::get_id(); });

    std::thread emitter([&]() {
        emitterThread = std::this_thread::get_id();
        signal.emit(1);
    });
    emitter.join();

    EXPECT_EQ(callbackThread, emitterThread);
}

TEST_F(ConnectionFixture, autoConnectionSkipsReceiverMarkedToDelete)
{
    int externalInvocations = 0;
    auto* receiver = new ValueReceiver(&externalInvocations);
    EventLoop* mainLoop = receiver->ownerEventLoop();

    Signal<int> signal;
    signal.connect(NodePtr<ValueReceiver>(receiver), &ValueReceiver::onValue, ConnectionType::Auto);

    receiver->deleteLater();
    signal.emit(1);                                       // same thread: direct path
    std::thread emitter([&]() { signal.emit(2); });       // other thread: queued path
    emitter.join();
    mainLoop->runPendingWork();
    app->loopOnce();

    EXPECT_EQ(externalInvocations, 0);
    EXPECT_FALSE(NodePtr<ValueReceiver>(receiver));
}

TEST_F(ConnectionFixture, autoConnectionDropsQueuedDeliveryIfReceiverIsDestroyedFirst)
{
    int externalInvocations = 0;
    auto* receiver = new ValueReceiver(&externalInvocations);
    EventLoop* mainLoop = receiver->ownerEventLoop();

    Signal<int> signal;
    signal.connect(NodePtr<ValueReceiver>(receiver), &ValueReceiver::onValue, ConnectionType::Auto);

    std::thread emitter([&]() { signal.emit(3); });  // queued on main
    emitter.join();

    delete receiver;  // before main processes the delivery
    mainLoop->runPendingWork();

    EXPECT_EQ(externalInvocations, 0);
}
