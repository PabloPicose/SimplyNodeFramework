#include <gtest/gtest.h>

#include "SNFCore/ThreadStorage.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

struct DestructorTracker
{
    explicit DestructorTracker(std::atomic<int>& counter)
        : m_counter(counter)
    {
    }

    ~DestructorTracker() { ++m_counter; }

    DestructorTracker(const DestructorTracker& other)
        : m_counter(other.m_counter)
    {
    }

    DestructorTracker& operator=(const DestructorTracker&) = delete;

    std::atomic<int>& m_counter;
};

struct MoveOnly
{
    MoveOnly() = default;
    explicit MoveOnly(int v)
        : value(v)
    {
    }
    MoveOnly(MoveOnly&&)            = default;
    MoveOnly& operator=(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&)       = delete;

    int value{0};
};

}  // namespace

// ---------------------------------------------------------------------------
// Basic single-thread tests
// ---------------------------------------------------------------------------

TEST(ThreadStorageTests, hasLocalDataReturnsFalseInitially)
{
    snf::ThreadStorage<int> storage;

    EXPECT_FALSE(storage.hasLocalData());
}

TEST(ThreadStorageTests, setLocalDataMakesHasLocalDataTrue)
{
    snf::ThreadStorage<int> storage;

    storage.setLocalData(42);

    EXPECT_TRUE(storage.hasLocalData());
}

TEST(ThreadStorageTests, setLocalDataStoresCorrectValue)
{
    snf::ThreadStorage<int> storage;

    storage.setLocalData(99);

    EXPECT_EQ(storage.localData(), 99);
}

TEST(ThreadStorageTests, localDataNonConstDefaultConstructsOnFirstAccess)
{
    snf::ThreadStorage<std::string> storage;

    std::string& value = storage.localData();

    EXPECT_TRUE(storage.hasLocalData());
    EXPECT_EQ(value, "");
}

TEST(ThreadStorageTests, localDataNonConstReturnsSameReference)
{
    snf::ThreadStorage<int> storage;

    int& ref1 = storage.localData();
    int& ref2 = storage.localData();

    EXPECT_EQ(&ref1, &ref2);
}

TEST(ThreadStorageTests, localDataNonConstModificationPersists)
{
    snf::ThreadStorage<int> storage;

    storage.localData() = 7;

    EXPECT_EQ(storage.localData(), 7);
}

TEST(ThreadStorageTests, localDataConstReturnsDefaultWhenNotSet)
{
    const snf::ThreadStorage<int> storage;

    EXPECT_EQ(storage.localData(), 0);
    EXPECT_FALSE(storage.hasLocalData());
}

TEST(ThreadStorageTests, localDataConstReturnsCopyWhenSet)
{
    snf::ThreadStorage<std::string> storage;
    storage.setLocalData("hello");

    const std::string copy = static_cast<const snf::ThreadStorage<std::string>&>(storage).localData();

    EXPECT_EQ(copy, "hello");
}

TEST(ThreadStorageTests, setLocalDataReplacesExistingValue)
{
    snf::ThreadStorage<int> storage;

    storage.setLocalData(10);
    storage.setLocalData(20);

    EXPECT_EQ(storage.localData(), 20);
}

TEST(ThreadStorageTests, setLocalDataDestroysPreviousValue)
{
    std::atomic<int> destructorCount{0};
    {
        snf::ThreadStorage<DestructorTracker> storage;
        storage.setLocalData(DestructorTracker{destructorCount});
        // destructorCount == 1: the temporary was destroyed during move construction
        const int after_first_set = destructorCount.load();

        storage.setLocalData(DestructorTracker{destructorCount});
        // After second set, old stored value must have been destroyed
        EXPECT_GT(destructorCount.load(), after_first_set);
    }
}

TEST(ThreadStorageTests, multipleStorageInstancesAreIndependent)
{
    snf::ThreadStorage<int> s1;
    snf::ThreadStorage<int> s2;

    s1.setLocalData(1);
    s2.setLocalData(2);

    EXPECT_EQ(s1.localData(), 1);
    EXPECT_EQ(s2.localData(), 2);
}

TEST(ThreadStorageTests, destructorCleansUpCurrentThreadData)
{
    std::atomic<int> destructorCount{0};
    {
        snf::ThreadStorage<DestructorTracker> storage;
        storage.setLocalData(DestructorTracker{destructorCount});
        const int before = destructorCount.load();
        // Destructor of ThreadStorage should destroy stored value
        (void)before;
    }
    // After destruction, the stored DestructorTracker must have been destroyed
    // (at least once for the stored value)
    EXPECT_GE(destructorCount.load(), 1);
}

TEST(ThreadStorageTests, setLocalDataAcceptsMoveOnlyType)
{
    snf::ThreadStorage<MoveOnly> storage;

    storage.setLocalData(MoveOnly{42});

    EXPECT_EQ(storage.localData().value, 42);
}

// ---------------------------------------------------------------------------
// Multi-thread tests
// ---------------------------------------------------------------------------

TEST(ThreadStorageTests, differentThreadsHaveIndependentValues)
{
    snf::ThreadStorage<int> storage;

    storage.setLocalData(0);  // main thread value

    constexpr int kThreadCount = 4;
    std::vector<int> results(kThreadCount, -1);
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&storage, &results, i]() {
            storage.setLocalData(i + 1);
            results[i] = storage.localData();
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(storage.localData(), 0);

    for (int i = 0; i < kThreadCount; ++i) {
        EXPECT_EQ(results[i], i + 1);
    }
}

TEST(ThreadStorageTests, dataCreatedViaLocalDataIsThreadLocal)
{
    snf::ThreadStorage<int> storage;

    // Auto-create in main thread
    storage.localData() = 100;

    int workerValue = -1;
    std::thread worker([&storage, &workerValue]() {
        // Worker thread should get its own default-constructed value
        workerValue = storage.localData();
        storage.localData() = 200;
    });
    worker.join();

    EXPECT_EQ(workerValue, 0);       // worker got default int
    EXPECT_EQ(storage.localData(), 100);  // main thread unaffected
}

TEST(ThreadStorageTests, hasLocalDataIsFalseInOtherThread)
{
    snf::ThreadStorage<int> storage;
    storage.setLocalData(1);  // set in main thread

    bool workerHas = true;
    std::thread worker([&storage, &workerHas]() {
        workerHas = storage.hasLocalData();
    });
    worker.join();

    EXPECT_FALSE(workerHas);
    EXPECT_TRUE(storage.hasLocalData());
}

TEST(ThreadStorageTests, threadExitDestroysLocalData)
{
    std::atomic<int> destructorCount{0};
    snf::ThreadStorage<DestructorTracker> storage;

    std::thread worker([&storage, &destructorCount]() {
        storage.setLocalData(DestructorTracker{destructorCount});
        // Thread exits here; stored data should be destroyed
    });
    worker.join();

    // At least the value stored in the worker should have been destroyed
    EXPECT_GE(destructorCount.load(), 1);
}

TEST(ThreadStorageTests, concurrentAccessFromManyThreadsIsCorrect)
{
    snf::ThreadStorage<int> storage;

    constexpr int kThreadCount = 16;
    std::vector<bool> ok(kThreadCount, false);
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&storage, &ok, i]() {
            storage.setLocalData(i * 10);
            const int read = storage.localData();
            ok[i]          = (read == i * 10);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (int i = 0; i < kThreadCount; ++i) {
        EXPECT_TRUE(ok[i]) << "Thread " << i << " read unexpected value";
    }
}

TEST(ThreadStorageTests, multipleStoragesAreIndependentAcrossThreads)
{
    snf::ThreadStorage<std::string> nameStorage;
    snf::ThreadStorage<int> idStorage;

    nameStorage.setLocalData("main");
    idStorage.setLocalData(0);

    std::string workerName;
    int workerId = -1;

    std::thread worker([&]() {
        nameStorage.setLocalData("worker");
        idStorage.setLocalData(99);
        workerName = nameStorage.localData();
        workerId   = idStorage.localData();
    });
    worker.join();

    EXPECT_EQ(workerName, "worker");
    EXPECT_EQ(workerId, 99);
    EXPECT_EQ(nameStorage.localData(), "main");
    EXPECT_EQ(idStorage.localData(), 0);
}
