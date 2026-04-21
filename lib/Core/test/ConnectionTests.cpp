#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "SNFCore/Application.h"
#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
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
    app->run();

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
