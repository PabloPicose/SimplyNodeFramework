
#include <gtest/gtest.h>

#include <thread>

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Node.h"
#include "SNFCore/NodePtr.h"
// #include "SNFCore/Timer.h"

using namespace snf;

class TestNode final : public snf::Node {
 public:
  explicit TestNode(Node* parent = nullptr) : Node(parent) {}

 protected:
  void update() override {}
};

class CoreFixtureNoApp : public ::testing::Test {};

TEST_F(CoreFixtureNoApp, invalidAppInstance) {
  EXPECT_TRUE(Application::instance() == nullptr);
}

TEST_F(CoreFixtureNoApp, avoidWithoutApplicationInstance) {
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

class CoreFixture : public ::testing::Test {
 public:
  void SetUp() override { app = new Application(0, nullptr); }

  void TearDown() override {
    delete app;
    app = nullptr;
  }

  Application* app = nullptr;
};

// createApplicationInstance is covered by SetUp in CoreFixture.

TEST_F(CoreFixture, createNodeRoot) {
  EXPECT_TRUE(Application::instance() != nullptr);
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  {
    TestNode node;
    EXPECT_TRUE(node.parent() == nullptr);
    EXPECT_TRUE(node.childrenCount() == 0);
    EXPECT_TRUE(node.isRoot());

    // App instance has one root node
    EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
  }
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
}

TEST_F(CoreFixture, addChildConstructor) {
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  TestNode parent;
  TestNode child(&parent);
  EXPECT_TRUE(parent.childrenCount() == 1);
  EXPECT_TRUE(child.parent() == &parent);
  EXPECT_FALSE(child.isRoot());
  // the childs of child should be 0
  EXPECT_TRUE(child.childrenCount() == 0);
  // app instance has one root node
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
}

TEST_F(CoreFixture, addChildMethod) {
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  TestNode parent;
  TestNode child;
  parent.addChild(&child);
  EXPECT_TRUE(parent.childrenCount() == 1);
  EXPECT_TRUE(child.parent() == &parent);
  EXPECT_FALSE(child.isRoot());
  // the childs of child should be 0
  EXPECT_TRUE(child.childrenCount() == 0);
  // app instance has one root node
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
}

TEST_F(CoreFixture, setParent) {
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
  EXPECT_TRUE(parent.childrenCount() == 1);
  // The child should have the parent as parent
  EXPECT_EQ(child.parent(), &parent);
  // The child should not be root
  EXPECT_FALSE(child.isRoot());
}

// Is child
TEST_F(CoreFixture, isChild) {
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  TestNode parent;
  TestNode child;
  EXPECT_FALSE(parent.isChild(&child));
  parent.addChild(&child);
  EXPECT_TRUE(parent.isChild(&child));
}

// Get child test
TEST_F(CoreFixture, getChild) {
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
  EXPECT_TRUE(parent.childrenCount() == 1);
  // The child should have the parent as parent
  EXPECT_EQ(child.parent(), &parent);
  // The child should not be root
  EXPECT_FALSE(child.isRoot());
  // The parent should have 1 child
  EXPECT_TRUE(parent.childrenCount() == 1);
  // The child should be the same memory address
  EXPECT_EQ(parent.getChild(0), &child);
}

// TESTS TO DELETE LATER
// TEST of deletion and delete later of Node pointers
TEST_F(CoreFixture, nodePtrDeleteLater) {
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
  TestNode* parent = new TestNode();
  TestNode* child = new TestNode(parent);
  // The parent should be nullptr
  EXPECT_EQ(parent->parent(), nullptr);
  // The app should have 1 root node
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
  // The parent should have 1 child
  EXPECT_EQ(parent->childrenCount(), 1);
  // The child should have the parent as parent
  EXPECT_EQ(child->parent(), parent);
  // The child should not be root
  EXPECT_FALSE(child->isRoot());
  // The application should have 2 alive nodes and non marked to delete
  EXPECT_EQ(Application::instance()->getAliveNodesCount(), 2);
  EXPECT_EQ(Application::instance()->getAliveNodesToDeleteCount(), 0);

  // The deleteLater of the child should not delete the child
  child->deleteLater();
  // The application should not be aware of the child to delete
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
  EXPECT_EQ(parent->childrenToDeleteCount(), 1);
  EXPECT_TRUE(parent->getChild(0));
  {
    NodePtr nodePtr(child);
    EXPECT_TRUE(nodePtr);
  }
  // The parent::run function should delete the child
  parent->run();
  EXPECT_EQ(parent->childrenToDeleteCount(), 0);
  EXPECT_EQ(parent->childrenCount(), 0);
  // The app should have 1 root node
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
  // The child should be deleted
  {
    NodePtr nodePtr(child);
    EXPECT_FALSE(nodePtr);
  }

  // From the parent, if the App::run is called and the parent is marked to
  // delete later the parent should be deleted
  EXPECT_FALSE(parent->parent());
  EXPECT_TRUE(parent->isRoot());
  parent->deleteLater();
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 1);
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
  {
    NodePtr nodePtr(parent);
    EXPECT_TRUE(nodePtr);
  }
  Application::instance()->run();
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  {
    NodePtr nodePtr(parent);
    EXPECT_FALSE(nodePtr);
  }
}

// Test deletion
TEST_F(CoreFixture, NonPointerDeleteLater) {
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
  TestNode* parent = new TestNode();
  {
    TestNode child(parent);
    child.deleteLater();
  }
  // The application should not be aware of the child to delete
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
  // The parent::run function should not delete the child because the child is not a pointer
  parent->run();
  // The app should have 1 root node
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 1);
  // The node should have 0 children because the child is not a pointer and it should be deleted when it goes out of scope
  EXPECT_EQ(parent->childrenCount(), 0);
}

// TEST NodePtr
TEST_F(CoreFixture, NodePtrFunctionality) {
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
  TestNode* parent = new TestNode();
  TestNode* child = new TestNode(parent);
  // The NodePtr should be valid
  {
    NodePtr nodePtr(child);
    EXPECT_TRUE(nodePtr);
    EXPECT_TRUE(nodePtr.get() == child);
    EXPECT_TRUE(nodePtr.isAlive());
    EXPECT_FALSE(nodePtr.isMarkedToDelete());
  }
  // The deleteLater of the child should not delete the child
  child->deleteLater();
  // The application should not be aware of the child to delete
  {
    NodePtr nodePtr(child);
    EXPECT_TRUE(nodePtr);
    EXPECT_TRUE(nodePtr.get() == child);
    EXPECT_TRUE(nodePtr.isAlive());
    EXPECT_TRUE(nodePtr.isMarkedToDelete());
  }
  // The parent::run function should delete the child
  parent->run();
  // The child should be deleted
  {
    NodePtr nodePtr(child);
    EXPECT_FALSE(nodePtr);
    EXPECT_FALSE(nodePtr.isAlive());
    EXPECT_TRUE(nodePtr.isMarkedToDelete());
  }

  // From the parent, if the App::run is called and the parent is marked to
  // delete later the parent should be deleted
  EXPECT_FALSE(parent->parent());
  EXPECT_TRUE(parent->isRoot());
  // The NodePtr should be valid
  {
    NodePtr nodePtr(parent);
    EXPECT_TRUE(nodePtr);
    EXPECT_TRUE(nodePtr.get() == parent);
    EXPECT_TRUE(nodePtr.isAlive());
    EXPECT_FALSE(nodePtr.isMarkedToDelete());
  }
  parent->deleteLater();
  // test the parent pointer
  {
    NodePtr nodePtr(parent);
    EXPECT_TRUE(nodePtr);
    EXPECT_TRUE(nodePtr.get() == parent);
    EXPECT_TRUE(nodePtr.isAlive());
    EXPECT_TRUE(nodePtr.isMarkedToDelete());
  }
  Application::instance()->run();
  // The parent should be deleted
  {
    NodePtr nodePtr(parent);
    EXPECT_FALSE(nodePtr);
    EXPECT_FALSE(nodePtr.isAlive());
    EXPECT_TRUE(nodePtr.isMarkedToDelete());
  }
  // the app should have 0 root nodes
  EXPECT_EQ(Application::instance()->getRootNodesCount(), 0);
  // and 0 nodes to delete
  EXPECT_EQ(Application::instance()->getRootNodesToDeleteCount(), 0);
  EXPECT_EQ(Application::instance()->getAliveNodesCount(), 0);
}

TEST_F(CoreFixture, nodeCreatesEventLoopForCreationThread) {
  // Application constructor creates the main-thread EventLoop eagerly.
  EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);

  std::thread::id createdThreadId;
  std::thread::id ownerThreadId;
  bool hasThreadEventLoop = false;

  std::thread worker([&]() {
    createdThreadId = std::this_thread::get_id();
    TestNode node;
    ownerThreadId = node.ownerThreadId();
    hasThreadEventLoop =
        (Application::instance()->getEventLoopByThreadId(createdThreadId) != nullptr);
  });
  worker.join();

  EXPECT_EQ(ownerThreadId, createdThreadId);
  EXPECT_TRUE(hasThreadEventLoop);
  // Main-thread loop + worker-thread loop.
  EXPECT_EQ(Application::instance()->getEventLoopCount(), 2);
}

TEST_F(CoreFixture, deleteLaterFromOtherThreadDeletesOnOwnerEventLoopRun) {
  TestNode* node = new TestNode();
  ASSERT_NE(node, nullptr);

  const std::thread::id ownerThreadId = node->ownerThreadId();
  EventLoop* ownerLoop =
      Application::instance()->getEventLoopByThreadId(ownerThreadId);

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

TEST_F(CoreFixture, eventLoopIsReusedWithinSameThread) {
  // Application constructor creates the main-thread EventLoop eagerly: count is 1.
  EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);

  TestNode first;
  const std::thread::id firstOwner = first.ownerThreadId();
  // Still 1: no new loop created for main thread.
  EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);

  TestNode second;
  const std::thread::id secondOwner = second.ownerThreadId();
  EXPECT_EQ(firstOwner, secondOwner);
  EXPECT_EQ(Application::instance()->getEventLoopCount(), 1);
}

TEST_F(CoreFixture, deleteLaterIsIdempotent) {
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

TEST_F(CoreFixture, eventLoopStopWakesBlockedRun) {
  std::thread::id loopThreadId;
  bool runReturned = false;

  std::thread loopThread([&]() {
    Application::instance()->getOrCreateCurrentThreadEventLoop();
    loopThreadId = std::this_thread::get_id();
    EventLoop* loop = Application::instance()->getEventLoopByThreadId(loopThreadId);
    ASSERT_NE(loop, nullptr);
    loop->run();
    runReturned = true;
  });

  while (loopThreadId == std::thread::id()) {
    std::this_thread::yield();
  }

  EventLoop* loop = Application::instance()->getEventLoopByThreadId(loopThreadId);
  ASSERT_NE(loop, nullptr);
  loop->stop();

  loopThread.join();
  EXPECT_TRUE(runReturned);
}

TEST_F(CoreFixture, childInheritsParentAffinity) {
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

TEST_F(CoreFixture, deleteLaterOnChildAlsoDeletesGrandchild) {
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
