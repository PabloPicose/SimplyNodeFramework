
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/Timer.h"

using namespace snf;

class TestNode final : public snf::Node
{
public:
    explicit TestNode(Node* parent = nullptr) : Node(parent) {}

protected:
    void update() override {}
};

class CoreFixtureNoApp : public ::testing::Test
{
};

TEST_F(CoreFixtureNoApp, invalidAppInstance) { EXPECT_EQ(Application::instance(), nullptr); }

TEST_F(CoreFixtureNoApp, avoidWithoutApplicationInstance)
{
    try {
        TestNode node;
        node.deleteLater();
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& err) {
        EXPECT_TRUE(true);
    } catch (...) {
        FAIL() << "Expected std::runtime_error";
    }
}

class CoreFixture : public ::testing::Test
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

// createApplicationInstance is covered by SetUp in CoreFixture.

TEST_F(CoreFixture, createNodeRoot)
{
    EXPECT_NE(Application::instance(), nullptr);
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    {
        TestNode node;
        EXPECT_EQ(node.parent(), nullptr);
        EXPECT_EQ(node.childrenCount(), 0);
        EXPECT_TRUE(node.isRoot());

        // App instance has one root node
        EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
    }
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
}

TEST_F(CoreFixture, applicationRunUsesRegisteredRunLoopDriver)
{
    int owner = 0;
    bool called = false;

    Application::instance()->setRunLoopDriver(&owner, [&]() {
        called = true;
        return 7;
    });

    EXPECT_EQ(Application::instance()->run(), 7);
    EXPECT_TRUE(called);

    Application::instance()->clearRunLoopDriver(&owner);
}

TEST_F(CoreFixture, applicationAllowsOnlyOneRunLoopDriver)
{
    int firstOwner = 0;
    int secondOwner = 0;

    Application::instance()->setRunLoopDriver(&firstOwner, []() { return 0; });

    EXPECT_THROW(Application::instance()->setRunLoopDriver(&secondOwner, []() { return 0; }), std::runtime_error);

    Application::instance()->clearRunLoopDriver(&firstOwner);
}

TEST_F(CoreFixture, addChildConstructor)
{
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    TestNode parent;
    TestNode child(&parent);
    EXPECT_EQ(parent.childrenCount(), 1);
    EXPECT_EQ(child.parent(), &parent);
    EXPECT_FALSE(child.isRoot());
    // the childs of child should be 0
    EXPECT_EQ(child.childrenCount(), 0);
    // app instance has one root node
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
}

TEST_F(CoreFixture, setParent)
{
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    TestNode parent;
    TestNode child;
    // The child at this moment should be root
    EXPECT_TRUE(child.isRoot());
    // The app at this moment should have 2 root nodes
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 2);
    // At this moment isChild should return false
    EXPECT_FALSE(parent.isChild(&child));
    // Change the parent
    child.setParent(&parent);
    // At this moment isChild should return true
    EXPECT_TRUE(parent.isChild(&child));
    // The child at this moment should not be root
    EXPECT_FALSE(child.isRoot());
    // The app at this moment should have 1 root node
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
    // The parent should have 1 child
    EXPECT_EQ(parent.childrenCount(), 1);
    // The child should have the parent as parent
    EXPECT_EQ(child.parent(), &parent);
    // The child should not be root
    EXPECT_FALSE(child.isRoot());
}

// Is child
TEST_F(CoreFixture, isChild)
{
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    TestNode parent;
    TestNode child;
    EXPECT_FALSE(parent.isChild(&child));
    parent.addChild(&child);
    EXPECT_TRUE(parent.isChild(&child));
}

// Get child test
TEST_F(CoreFixture, getChild)
{
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    TestNode parent;
    TestNode child;
    EXPECT_FALSE(parent.getChild(0));
    parent.addChild(&child);
    // The child at this moment should not be root
    EXPECT_FALSE(child.isRoot());
    // The app at this moment should have 1 root node
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
    // The parent should have 1 child
    EXPECT_EQ(parent.childrenCount(), 1);
    // The child should have the parent as parent
    EXPECT_EQ(child.parent(), &parent);
    // The child should be the same memory address
    EXPECT_EQ(parent.getChild(0), &child);
}

TEST_F(CoreFixture, childDeleteLaterQueuesUnderParent)
{
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);

    TestNode* parent = new TestNode();
    TestNode* child = new TestNode(parent);

    EXPECT_EQ(parent->parent(), nullptr);
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
    EXPECT_EQ(parent->childrenCount(), 1);
    EXPECT_EQ(child->parent(), parent);
    EXPECT_FALSE(child->isRoot());
    EXPECT_EQ(Application::instance()->getAliveNodesCount(), 2);
    EXPECT_EQ(Application::instance()->getAliveNodesToDeleteCount(), 0);

    child->deleteLater();

    EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
    EXPECT_EQ(parent->childrenToDeleteCount(), 1);
    EXPECT_TRUE(parent->getChild(0));

    {
        NodePtr nodePtr(child);
        EXPECT_TRUE(nodePtr);
        EXPECT_TRUE(nodePtr.isAlive());
        EXPECT_TRUE(nodePtr.isMarkedToDelete());
    }

    // Cleanup root parent.
    parent->run();
    parent->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, parentRunDeletesQueuedChild)
{
    TestNode* parent = new TestNode();
    TestNode* child = new TestNode(parent);

    child->deleteLater();
    parent->run();

    EXPECT_EQ(parent->childrenToDeleteCount(), 0);
    EXPECT_EQ(parent->childrenCount(), 0);
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
    {
        NodePtr nodePtr(child);
        EXPECT_FALSE(nodePtr);
    }

    parent->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, applicationRunDeletesRootMarkedDeleteLater)
{
    TestNode* parent = new TestNode();

    EXPECT_FALSE(parent->parent());
    EXPECT_TRUE(parent->isRoot());

    parent->deleteLater();
    EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 1);
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);

    {
        NodePtr nodePtr(parent);
        EXPECT_TRUE(nodePtr);
        EXPECT_TRUE(nodePtr.isAlive());
        EXPECT_TRUE(nodePtr.isMarkedToDelete());
    }

    Application::instance()->run();

    EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    {
        NodePtr nodePtr(parent);
        EXPECT_FALSE(nodePtr);
    }
}

TEST_F(CoreFixture, stackChildDeleteLaterDoesNotQueueParentDelete)
{
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);

    TestNode* parent = new TestNode();
    {
        TestNode child(parent);
        EXPECT_EQ(parent->childrenCount(), 1);
        child.deleteLater();
        EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
    }

    EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
    parent->run();
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
    EXPECT_EQ(parent->childrenCount(), 0);

    parent->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, nodePtrStateTransitionsForRootNode)
{
    TestNode* parent = new TestNode();

    {
        NodePtr nodePtr(parent);
        EXPECT_TRUE(nodePtr);
        EXPECT_EQ(nodePtr.get(), parent);
        EXPECT_TRUE(nodePtr.isAlive());
        EXPECT_FALSE(nodePtr.isMarkedToDelete());
    }

    parent->deleteLater();

    {
        NodePtr nodePtr(parent);
        EXPECT_TRUE(nodePtr);
        EXPECT_EQ(nodePtr.get(), parent);
        EXPECT_TRUE(nodePtr.isAlive());
        EXPECT_TRUE(nodePtr.isMarkedToDelete());
    }

    Application::instance()->run();

    {
        NodePtr nodePtr(parent);
        EXPECT_FALSE(nodePtr);
        EXPECT_FALSE(nodePtr.isAlive());
        EXPECT_TRUE(nodePtr.isMarkedToDelete());
    }

    EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
    EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
    EXPECT_EQ(Application::instance()->getAliveNodesCount(), 0);
}

TEST_F(CoreFixture, nodeCreatesEventLoopForCreationThread)
{
    // Application constructor creates the main-thread EventLoop eagerly.
    EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);

    std::thread::id createdThreadId;
    std::thread::id ownerThreadId;
    bool hasThreadEventLoop = false;

    std::thread worker([&]() {
        createdThreadId = std::this_thread::get_id();
        TestNode node;
        ownerThreadId = node.ownerThreadId();
        hasThreadEventLoop = (Application::instance()->getEventLoopByThreadId(createdThreadId) != nullptr);
    });
    worker.join();

    EXPECT_EQ(ownerThreadId, createdThreadId);
    EXPECT_TRUE(hasThreadEventLoop);
    // Main-thread loop + worker-thread loop.
    EXPECT_EQ(Application::instance()->getEventLoopCount(), 2);
}

TEST_F(CoreFixture, deleteLaterFromOtherThreadDeletesOnOwnerEventLoopRun)
{
    TestNode* node = new TestNode();
    ASSERT_NE(node, nullptr);

    const std::thread::id ownerThreadId = node->ownerThreadId();
    EventLoop* ownerLoop = Application::instance()->getEventLoopByThreadId(ownerThreadId);

    ASSERT_NE(ownerLoop, nullptr);
    ASSERT_EQ(ownerThreadId, std::this_thread::get_id());

    std::thread worker([&]() { node->deleteLater(); });
    worker.join();

    {
        NodePtr<TestNode> nodePtr(node);
        EXPECT_TRUE(nodePtr);
        EXPECT_TRUE(nodePtr.isMarkedToDelete());
    }

    ownerLoop->run();

    {
        NodePtr<TestNode> nodePtr(node);
        EXPECT_FALSE(nodePtr);
    }
}

TEST_F(CoreFixture, eventLoopIsReusedWithinSameThread)
{
    // Application constructor creates the main-thread EventLoop eagerly: count is 1.
    EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);

    TestNode first;
    const std::thread::id firstOwner = first.ownerThreadId();
    EventLoop* firstLoop = first.ownerEventLoop();
    // Still 1: no new loop created for main thread.
    EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);

    TestNode second;
    const std::thread::id secondOwner = second.ownerThreadId();
    EventLoop* secondLoop = second.ownerEventLoop();
    EXPECT_EQ(firstOwner, secondOwner);
    EXPECT_EQ(firstLoop, secondLoop);
    EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);
}

TEST_F(CoreFixture, deleteLaterIsIdempotent)
{
    TestNode* node = new TestNode();
    ASSERT_NE(node, nullptr);

    node->deleteLater();
    node->deleteLater();

    {
        NodePtr<TestNode> nodePtr(node);
        EXPECT_TRUE(nodePtr);
        EXPECT_TRUE(nodePtr.isMarkedToDelete());
    }

    Application::instance()->run();

    {
        NodePtr<TestNode> nodePtr(node);
        EXPECT_FALSE(nodePtr);
    }
}

TEST_F(CoreFixture, eventLoopStopWakesBlockedRun)
{
    using namespace std::chrono_literals;

    std::thread::id loopThreadId;
    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady = false;
    bool runReturned = false;
    std::chrono::steady_clock::duration runDuration{};

    std::thread loopThread([&]() {
        Application::instance()->getOrCreateCurrentThreadEventLoop();
        loopThreadId = std::this_thread::get_id();
        EventLoop* loop = Application::instance()->getEventLoopByThreadId(loopThreadId);
        ASSERT_NE(loop, nullptr);

        // Keep this loop blocked waiting for timer deadline until stop() is called.
        Timer timer;
        timer.setSingleShot(true);
        timer.start(2s);

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        const auto startedAt = std::chrono::steady_clock::now();
        loop->run();
        runDuration = std::chrono::steady_clock::now() - startedAt;
        runReturned = true;
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return workerReady; });
    }

    EventLoop* loop = Application::instance()->getEventLoopByThreadId(loopThreadId);
    ASSERT_NE(loop, nullptr);
    loop->stop();

    loopThread.join();
    EXPECT_TRUE(runReturned);
    EXPECT_LT(runDuration, 1s);
}

TEST_F(CoreFixture, childInheritsParentAffinity)
{
    TestNode* parent = new TestNode();
    ASSERT_NE(parent, nullptr);
    const std::thread::id parentOwner = parent->ownerThreadId();
    EventLoop* parentLoop = parent->ownerEventLoop();
    ASSERT_NE(parentLoop, nullptr);

    TestNode* child = new TestNode(parent);
    ASSERT_NE(child, nullptr);

    EXPECT_EQ(child->ownerThreadId(), parentOwner);
    EXPECT_EQ(child->ownerEventLoop(), parentLoop);

    parent->deleteLater();
    Application::instance()->run();

    {
        NodePtr<TestNode> parentPtr(parent);
        EXPECT_FALSE(parentPtr);
    }
    {
        NodePtr<TestNode> childPtr(child);
        EXPECT_FALSE(childPtr);
    }
}

TEST_F(CoreFixture, deleteLaterOnChildAlsoDeletesGrandchild)
{
    TestNode* parent = new TestNode();
    TestNode* child = new TestNode(parent);
    TestNode* grandchild = new TestNode(child);

    EXPECT_EQ(Application::instance()->getAliveNodesCount(), 3);
    EXPECT_EQ(parent->childrenCount(), 1);
    EXPECT_EQ(child->childrenCount(), 1);

    // deleteLater on child: grandchild must be destroyed together with child
    child->deleteLater();

    // Before parent->run() nothing has been deleted yet
    {
        NodePtr<TestNode> childPtr(child);
        EXPECT_TRUE(childPtr);
        EXPECT_TRUE(childPtr.isMarkedToDelete());
    }
    {
        NodePtr<TestNode> grandchildPtr(grandchild);
        EXPECT_TRUE(grandchildPtr);
    }
    EXPECT_EQ(Application::instance()->getAliveNodesCount(), 3);

    // parent->run() drains childrenToDelete, which cascades into grandchild
    parent->run();

    {
        NodePtr<TestNode> childPtr(child);
        EXPECT_FALSE(childPtr);
    }
    {
        NodePtr<TestNode> grandchildPtr(grandchild);
        EXPECT_FALSE(grandchildPtr);
    }
    // Only parent remains alive
    EXPECT_EQ(Application::instance()->getAliveNodesCount(), 1);
    EXPECT_EQ(parent->childrenCount(), 0);

    // Clean up parent
    parent->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, eventLoopHasPendingWorkWithTask)
{
    EventLoop* loop = Application::instance()->getOrCreateCurrentThreadEventLoop();
    ASSERT_NE(loop, nullptr);

    EXPECT_FALSE(loop->hasPendingWork());

    loop->post([]() {});

    EXPECT_TRUE(loop->hasPendingWork());

    loop->run();

    EXPECT_FALSE(loop->hasPendingWork());
}

TEST_F(CoreFixture, eventLoopHasPendingWorkWithDelete)
{
    TestNode* node = new TestNode();
    EventLoop* loop = node->ownerEventLoop();
    ASSERT_NE(loop, nullptr);

    EXPECT_FALSE(loop->hasPendingWork());

    node->deleteLater();

    EXPECT_TRUE(loop->hasPendingWork());

    loop->run();

    EXPECT_FALSE(loop->hasPendingWork());
}

TEST_F(CoreFixture, eventLoopHasPendingWorkWithTimer)
{
    using namespace std::chrono_literals;

    Timer timer;
    EventLoop* loop = timer.ownerEventLoop();
    ASSERT_NE(loop, nullptr);

    EXPECT_FALSE(loop->hasPendingWork());

    timer.setSingleShot(true);
    timer.start(100ms);

    EXPECT_TRUE(loop->hasPendingWork());

    timer.stop();

    EXPECT_FALSE(loop->hasPendingWork());
}

TEST_F(CoreFixture, allEventLoopsIdleWhenNoWork) { EXPECT_TRUE(Application::instance()->allEventLoopsIdle()); }

TEST_F(CoreFixture, allEventLoopsIdleReturnsFalseWhenWorkerHasWork)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool posted = false;
    bool allowRun = false;

    std::thread worker([&]() {
        EventLoop* loop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            loop->post([]() {});
            posted = true;
        }
        cv.notify_one();

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return allowRun; });
        }

        loop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return posted; });
    }

    EXPECT_FALSE(Application::instance()->allEventLoopsIdle());

    {
        std::lock_guard<std::mutex> lock(mutex);
        allowRun = true;
    }
    cv.notify_one();

    worker.join();

    EXPECT_TRUE(Application::instance()->allEventLoopsIdle());
}

TEST_F(CoreFixture, quitStopsAllEventLoops)
{
    using namespace std::chrono_literals;

    std::thread::id workerThreadId;
    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady = false;
    bool runReturned = false;
    std::chrono::steady_clock::duration runDuration{};

    std::thread worker([&]() {
        workerThreadId = std::this_thread::get_id();
        EventLoop* loop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(loop, nullptr);

        // Keep the loop blocked on a future deadline so quit() must wake it.
        Timer timer;
        timer.setSingleShot(true);
        timer.start(std::chrono::seconds(2));

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        const auto startedAt = std::chrono::steady_clock::now();
        loop->run();
        runDuration = std::chrono::steady_clock::now() - startedAt;
        runReturned = true;
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return workerReady; });
    }

    // Wait until the worker has registered its EventLoop
    while (Application::instance()->getEventLoopByThreadId(workerThreadId) == nullptr) {
        std::this_thread::yield();
    }

    Application::instance()->quit();
    worker.join();

    EXPECT_TRUE(runReturned);
    EXPECT_LT(runDuration, std::chrono::seconds(1));
}

TEST_F(CoreFixture, quitStopsMultipleWorkerEventLoopsWithPendingTimers)
{
    using namespace std::chrono_literals;

    constexpr std::size_t kWorkerCount = 4;

    std::mutex mutex;
    std::condition_variable cv;
    std::size_t readyCount = 0;

    std::vector<std::thread> workers;
    workers.reserve(kWorkerCount);

    std::vector<std::thread::id> workerThreadIds(kWorkerCount);
    std::vector<std::atomic_bool> runReturned(kWorkerCount);
    std::vector<std::chrono::steady_clock::duration> runDurations(kWorkerCount);

    for (std::size_t i = 0; i < kWorkerCount; ++i) {
        runReturned[i].store(false, std::memory_order_relaxed);
    }

    for (std::size_t i = 0; i < kWorkerCount; ++i) {
        workers.emplace_back([&, i]() {
            workerThreadIds[i] = std::this_thread::get_id();

            EventLoop* loop = Application::instance()->getOrCreateCurrentThreadEventLoop();
            ASSERT_NE(loop, nullptr);

            // Keep the loop waiting on a future deadline so quit() must wake it.
            Timer timer;
            timer.setSingleShot(true);
            timer.start(5s);

            {
                std::lock_guard<std::mutex> lock(mutex);
                ++readyCount;
            }
            cv.notify_one();

            const auto startedAt = std::chrono::steady_clock::now();
            loop->run();
            runDurations[i] = std::chrono::steady_clock::now() - startedAt;
            runReturned[i].store(true, std::memory_order_release);
        });
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return readyCount == kWorkerCount; });
    }

    // Ensure all worker loops are registered before quit().
    for (const std::thread::id workerThreadId : workerThreadIds) {
        while (Application::instance()->getEventLoopByThreadId(workerThreadId) == nullptr) {
            std::this_thread::yield();
        }
    }

    EXPECT_FALSE(Application::instance()->allEventLoopsIdle());

    Application::instance()->quit();

    for (std::thread& worker : workers) {
        worker.join();
    }

    for (std::size_t i = 0; i < kWorkerCount; ++i) {
        EXPECT_TRUE(runReturned[i].load(std::memory_order_acquire));
        EXPECT_LT(runDurations[i], 1s);
    }
}

// ---- Generation ID tests ----

TEST_F(CoreFixture, nodeHasNonZeroGeneration)
{
    TestNode* node = new TestNode();
    EXPECT_GT(node->generation(), 0u);

    node->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, differentNodesHaveDistinctGenerations)
{
    TestNode* a = new TestNode();
    TestNode* b = new TestNode();

    EXPECT_NE(a->generation(), b->generation());

    a->deleteLater();
    b->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, nodePtrCapturesGenerationAtCreation)
{
    TestNode* node = new TestNode();
    const std::uint64_t capturedGen = node->generation();

    NodePtr<TestNode> ptr(node);

    EXPECT_TRUE(ptr);
    EXPECT_EQ(capturedGen, node->generation());

    node->deleteLater();
    Application::instance()->run();

    // After deletion the ptr must be dead even though we still hold the raw pointer.
    EXPECT_FALSE(ptr);
    EXPECT_FALSE(ptr.isAlive());
}

// ABA protection: a NodePtr created for node A must remain dead after A is deleted,
// even if a brand-new node happens to be allocated at the exact same address.
// We verify the mechanism by asserting the new node has a strictly greater generation,
// which is the invariant that prevents the address-reuse false-positive.
TEST_F(CoreFixture, nodePtrDeadAfterABAAddressReuse)
{
    TestNode* nodeA = new TestNode();
    const std::uint64_t genA = nodeA->generation();
    NodePtr<TestNode> ptrA(nodeA);

    EXPECT_TRUE(ptrA);

    nodeA->deleteLater();
    Application::instance()->run();

    EXPECT_FALSE(ptrA);

    // Allocate a new node — generation must be strictly greater than genA,
    // so a NodePtr to genA will correctly return false even at the same address.
    TestNode* nodeB = new TestNode();
    EXPECT_GT(nodeB->generation(), genA);

    // ptrA still dead — generation mismatch protects against ABA.
    EXPECT_FALSE(ptrA);

    nodeB->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, nodePtrIsAliveReflectsMarkedToDeleteState)
{
    TestNode* node = new TestNode();
    NodePtr<TestNode> ptr(node);

    // Before deleteLater: alive, not marked.
    EXPECT_TRUE(ptr.isAlive());
    EXPECT_FALSE(ptr.isMarkedToDelete());
    EXPECT_TRUE(ptr);

    node->deleteLater();

    // After deleteLater but before run: still alive (memory valid), but marked.
    EXPECT_TRUE(ptr.isAlive());
    EXPECT_TRUE(ptr.isMarkedToDelete());
    EXPECT_TRUE(ptr);

    Application::instance()->run();

    // After run: dead.
    EXPECT_FALSE(ptr.isAlive());
    EXPECT_TRUE(ptr.isMarkedToDelete());
    EXPECT_FALSE(ptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// moveToThread tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CoreFixture, moveToThreadSameThreadIsNoOp)
{
    // Moving to the same thread must succeed and leave affinity unchanged.
    TestNode* node = new TestNode();
    const std::thread::id originalThread = node->ownerThreadId();
    EventLoop* originalLoop              = node->ownerEventLoop();

    EXPECT_TRUE(node->moveToThread(originalThread));
    EXPECT_EQ(node->ownerThreadId(), originalThread);
    EXPECT_EQ(node->ownerEventLoop(), originalLoop);

    node->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, moveToThreadToUnknownThreadReturnsFalse)
{
    // A thread that never called getOrCreateCurrentThreadEventLoop has no loop.
    std::thread::id unknownId;
    {
        std::thread t([&]() { unknownId = std::this_thread::get_id(); });
        t.join();
    }

    TestNode* node = new TestNode();
    EXPECT_FALSE(node->moveToThread(unknownId));

    node->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, moveToThreadChangesOwnerThreadId)
{
    using namespace std::chrono_literals;

    // Synchronisation primitives.
    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady = false;
    bool migrationDone = false;
    std::thread::id workerThreadId;

    // Step 1: spin up a worker that creates an EventLoop and waits.
    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        // Keep the loop alive until the test posts a stop.
        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(3s);

        workerLoop->run();
    });

    // Wait until worker loop is up.
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    // Step 2: create a root node on the main thread.
    TestNode* node = new TestNode();
    const std::thread::id mainThread = node->ownerThreadId();
    EXPECT_EQ(mainThread, std::this_thread::get_id());

    // Step 3: move the node to the worker thread.
    EXPECT_TRUE(node->moveToThread(workerThreadId));

    // Give the posted migration task a chance to run.
    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);

    // Drain the worker's task queue by posting a sentinel that stops the loop
    // and signals the main thread.
    workerLoop->post([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        migrationDone = true;
        cv.notify_one();
        workerLoop->stop();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return migrationDone; });
    }

    worker.join();

    // After migration the node must report the worker thread.
    EXPECT_EQ(node->ownerThreadId(), workerThreadId);
    EXPECT_EQ(node->ownerEventLoop(), workerLoop);

    // Cleanup: delete the node (it now lives on the worker thread loop, but
    // the worker exited, so we just delete synchronously).
    delete node;
}

TEST_F(CoreFixture, moveToThreadSubtreeMigratesAllDescendants)
{
    using namespace std::chrono_literals;

    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady   = false;
    bool migrationDone = false;
    std::thread::id workerThreadId;

    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(3s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    // Build a small subtree: root → child → grandchild.
    TestNode* root       = new TestNode();
    TestNode* child      = new TestNode(root);
    TestNode* grandchild = new TestNode(child);

    EXPECT_TRUE(root->moveToThread(workerThreadId));

    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);

    workerLoop->post([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        migrationDone = true;
        cv.notify_one();
        workerLoop->stop();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return migrationDone; });
    }

    worker.join();

    // All three nodes must share the worker thread's affinity.
    EXPECT_EQ(root->ownerThreadId(), workerThreadId);
    EXPECT_EQ(child->ownerThreadId(), workerThreadId);
    EXPECT_EQ(grandchild->ownerThreadId(), workerThreadId);
    EXPECT_EQ(root->ownerEventLoop(), workerLoop);
    EXPECT_EQ(child->ownerEventLoop(), workerLoop);
    EXPECT_EQ(grandchild->ownerEventLoop(), workerLoop);

    delete root;
}

TEST_F(CoreFixture, moveToThreadRootNodeAppearsInTargetLoopRootList)
{
    using namespace std::chrono_literals;

    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady   = false;
    bool migrationDone = false;
    std::thread::id workerThreadId;

    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(3s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    TestNode* node = new TestNode();
    EXPECT_TRUE(node->isRoot());

    EventLoop* mainLoop   = node->ownerEventLoop();
    std::size_t rootsBefore = mainLoop->getRootNodesCount();

    EXPECT_TRUE(node->moveToThread(workerThreadId));

    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);

    workerLoop->post([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        migrationDone = true;
        cv.notify_one();
        workerLoop->stop();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return migrationDone; });
    }

    worker.join();

    // The root node must be in the worker loop's root list and removed from main.
    EXPECT_EQ(mainLoop->getRootNodesCount(), rootsBefore - 1);
    EXPECT_GE(workerLoop->getRootNodesCount(), std::size_t{1});

    delete node;
}

TEST_F(CoreFixture, moveToThreadRejectsNodeWhoseParentIsOnDifferentThread)
{
    using namespace std::chrono_literals;

    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady = false;
    std::thread::id workerThreadId;

    std::thread worker([&]() {
        Application::instance()->getOrCreateCurrentThreadEventLoop();
        workerThreadId = std::this_thread::get_id();
        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }
    worker.join();

    // Create a parent/child pair on main thread.
    TestNode* parent = new TestNode();
    TestNode* child  = new TestNode(parent);

    // Trying to migrate only the child while the parent stays on main must fail.
    EXPECT_FALSE(child->moveToThread(workerThreadId));

    parent->deleteLater();
    Application::instance()->run();
}

TEST_F(CoreFixture, moveToThreadFromOtherThreadIsAsynchronous)
{
    using namespace std::chrono_literals;

    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady   = false;
    bool migrationDone = false;
    std::thread::id workerThreadId;

    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(3s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    // Create a node on the main thread.
    TestNode* node = new TestNode();
    EventLoop* mainLoop   = node->ownerEventLoop();
    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);

    // Call moveToThread from a third thread (not the node owner) targeting workerLoop.
    std::thread caller([&]() {
        // This posts to the main loop.
        EXPECT_TRUE(node->moveToThread(workerThreadId));
    });
    caller.join();

    // The migration is now enqueued on the main loop. Run the main loop to apply it.
    mainLoop->run();

    // Check the node is now on the worker thread.
    EXPECT_EQ(node->ownerThreadId(), workerThreadId);
    EXPECT_EQ(node->ownerEventLoop(), workerLoop);

    // Stop worker and clean up.
    workerLoop->post([workerLoop]() { workerLoop->stop(); });
    worker.join();

    delete node;
}

TEST_F(CoreFixture, queuedConnectionStillDeliversOnNewThreadAfterMigration)
{
    using namespace std::chrono_literals;

    std::mutex mutex;
    std::condition_variable cv;
    bool workerReady   = false;
    bool receiverReady = false;
    bool received      = false;
    std::thread::id workerThreadId;
    std::thread::id callbackThreadId;

    // Worker thread hosts the receiver after migration.
    std::thread worker([&]() {
        EventLoop* workerLoop = Application::instance()->getOrCreateCurrentThreadEventLoop();
        ASSERT_NE(workerLoop, nullptr);
        workerThreadId = std::this_thread::get_id();

        {
            std::lock_guard<std::mutex> lock(mutex);
            workerReady = true;
        }
        cv.notify_one();

        Timer keepAlive;
        keepAlive.setSingleShot(true);
        keepAlive.start(3s);

        workerLoop->run();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return workerReady; });
    }

    // Create a simple receiver node on the main thread.
    struct Receiver final : Node {
        std::thread::id* cbThread;
        bool* receivedFlag;
        std::condition_variable* cv;
        std::mutex* mx;
        bool* readyFlag;
        explicit Receiver(std::thread::id* t, bool* r, std::condition_variable* c, std::mutex* m, bool* ready)
            : Node(), cbThread(t), receivedFlag(r), cv(c), mx(m), readyFlag(ready) {}
        void onValue() {
            *cbThread     = std::this_thread::get_id();
            *receivedFlag = true;
            if (EventLoop* loop = ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
        void update() override {}
    };

    Signal<> sig;
    Receiver* receiver = new Receiver(
        &callbackThreadId, &received, &cv, &mutex, &receiverReady);

    // Connect with Queued delivery.
    sig.connect(NodePtr<Receiver>(receiver), &Receiver::onValue, ConnectionType::Queued);

    // Migrate receiver to worker thread (synchronous — we're on owner thread).
    EventLoop* workerLoop = Application::instance()->getEventLoopByThreadId(workerThreadId);
    ASSERT_NE(workerLoop, nullptr);
    EXPECT_TRUE(receiver->moveToThread(workerThreadId));

    // Emit the signal; the queued delivery must land on the worker thread.
    sig.emit();

    worker.join();

    EXPECT_TRUE(received);
    EXPECT_EQ(callbackThreadId, workerThreadId);

    delete receiver;
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}
