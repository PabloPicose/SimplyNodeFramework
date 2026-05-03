#include <gtest/gtest.h>

#include "SNFExperimental/AsyncTaskSequence.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

class LambdaAsyncTask final : public snf::AsyncTask
{
public:
    using Body = std::function<void(const snf::AsyncTaskContext&, snf::AsyncTaskContext&)>;

    explicit LambdaAsyncTask(Body body)
        : m_body(std::move(body))
    {
    }

protected:
    void run(const snf::AsyncTaskContext& input, snf::AsyncTaskContext& output) override
    {
        if (m_body) {
            m_body(input, output);
        }
    }

private:
    Body m_body;
};

}  // namespace

TEST(AsyncTaskSequenceTests, fanInTwentyTasksIntoOneExitOnBoundedThreadPool)
{
    snf::ThreadPool pool(2);
    snf::AsyncTaskSequence sequence;
    std::atomic<int> completedLeaves{0};
    std::atomic<int> finalRuns{0};
    std::atomic<int> finishedSignals{0};
    std::mutex threadsMutex;
    std::set<std::thread::id> workerThreads;
    std::vector<snf::AsyncTaskSequence::TaskId> leaves;

    for (int i = 0; i < 20; ++i) {
        const auto id = sequence.addTask(std::make_shared<LambdaAsyncTask>(
            [&](const snf::AsyncTaskContext&, snf::AsyncTaskContext&) {
                {
                    std::lock_guard<std::mutex> lock(threadsMutex);
                    workerThreads.insert(std::this_thread::get_id());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++completedLeaves;
            }));
        ASSERT_NE(id, snf::AsyncTaskSequence::invalidTaskId);
        leaves.push_back(id);
    }

    const auto exitId = sequence.addTask(std::make_shared<LambdaAsyncTask>(
        [&](const snf::AsyncTaskContext& input, snf::AsyncTaskContext& output) {
            {
                std::lock_guard<std::mutex> lock(threadsMutex);
                workerThreads.insert(std::this_thread::get_id());
            }
            EXPECT_EQ(input.dependencyTaskIds().size(), 20U);
            EXPECT_EQ(completedLeaves.load(), 20);
            output.set("final", true);
            ++finalRuns;
        }));
    ASSERT_NE(exitId, snf::AsyncTaskSequence::invalidTaskId);

    for (const auto leaf : leaves) {
        ASSERT_TRUE(sequence.addDependency(leaf, exitId));
    }

    sequence.finished.connect([&]() { ++finishedSignals; });

    ASSERT_TRUE(sequence.hasValidGraph());
    ASSERT_TRUE(sequence.start(&pool));
    sequence.wait();
    pool.waitForDone();

    EXPECT_TRUE(sequence.isFinished());
    EXPECT_EQ(sequence.finishedTaskCount(), 21U);
    EXPECT_EQ(completedLeaves.load(), 20);
    EXPECT_EQ(finalRuns.load(), 1);
    EXPECT_EQ(finishedSignals.load(), 1);
    EXPECT_EQ(sequence.output(exitId).valueOr<bool>("final", false), true);
    EXPECT_LE(workerThreads.size(), 2U);
}

TEST(AsyncTaskSequenceTests, fanOutOneTaskIntoManyTerminalTasks)
{
    snf::ThreadPool pool(2);
    snf::AsyncTaskSequence sequence;
    std::atomic<int> terminalRuns{0};
    std::atomic<int> finishedSignals{0};

    const auto root = sequence.addTask(std::make_shared<LambdaAsyncTask>(
        [](const snf::AsyncTaskContext&, snf::AsyncTaskContext& output) {
            output.set("base", 7);
        }));
    ASSERT_NE(root, snf::AsyncTaskSequence::invalidTaskId);

    std::vector<snf::AsyncTaskSequence::TaskId> terminals;
    for (int i = 0; i < 20; ++i) {
        const auto id = sequence.addTask(std::make_shared<LambdaAsyncTask>(
            [&](const snf::AsyncTaskContext& input, snf::AsyncTaskContext& output) {
                EXPECT_EQ(input.valueOr<int>("base", 0), 7);
                output.set("result", input.valueOr<int>("base", 0) + 1);
                ++terminalRuns;
            }));
        ASSERT_NE(id, snf::AsyncTaskSequence::invalidTaskId);
        ASSERT_TRUE(sequence.addDependency(root, id));
        terminals.push_back(id);
    }

    sequence.finished.connect([&]() { ++finishedSignals; });

    ASSERT_TRUE(sequence.hasValidGraph());
    ASSERT_TRUE(sequence.start(&pool));
    sequence.wait();
    pool.waitForDone();

    EXPECT_TRUE(sequence.isFinished());
    EXPECT_EQ(sequence.finishedTaskCount(), 21U);
    EXPECT_EQ(terminalRuns.load(), 20);
    EXPECT_EQ(finishedSignals.load(), 1);
    for (const auto id : terminals) {
        EXPECT_EQ(sequence.output(id).valueOr<int>("result", 0), 8);
    }
}

TEST(AsyncTaskSequenceTests, outputContextCanPassDataThroughDependencies)
{
    snf::ThreadPool pool(2);
    snf::AsyncTaskSequence sequence;

    const auto producer = sequence.addTask(std::make_shared<LambdaAsyncTask>(
        [](const snf::AsyncTaskContext&, snf::AsyncTaskContext& output) {
            output.set("number", 41);
        }));
    const auto consumer = sequence.addTask(std::make_shared<LambdaAsyncTask>(
        [producer](const snf::AsyncTaskContext& input, snf::AsyncTaskContext& output) {
            const snf::AsyncTaskContext* producerOutput = input.dependencyOutput(producer);
            ASSERT_NE(producerOutput, nullptr);
            EXPECT_EQ(producerOutput->valueOr<int>("number", 0), 41);
            output.set("answer", input.valueOr<int>("number", 0) + 1);
        }));

    ASSERT_TRUE(sequence.addDependency(producer, consumer));
    ASSERT_TRUE(sequence.start(&pool));
    sequence.wait();
    pool.waitForDone();

    EXPECT_EQ(sequence.output(consumer).valueOr<int>("answer", 0), 42);
}

TEST(AsyncTaskSequenceTests, multipleTerminalTasksAreValidButCyclesAreRejected)
{
    snf::AsyncTaskSequence multipleTerminals;
    const auto root = multipleTerminals.addTask(std::make_shared<LambdaAsyncTask>(
        [](const snf::AsyncTaskContext&, snf::AsyncTaskContext&) {}));
    const auto first = multipleTerminals.addTask(std::make_shared<LambdaAsyncTask>(
        [](const snf::AsyncTaskContext&, snf::AsyncTaskContext&) {}));
    const auto second = multipleTerminals.addTask(std::make_shared<LambdaAsyncTask>(
        [](const snf::AsyncTaskContext&, snf::AsyncTaskContext&) {}));

    ASSERT_TRUE(multipleTerminals.addDependency(root, first));
    ASSERT_TRUE(multipleTerminals.addDependency(root, second));
    EXPECT_TRUE(multipleTerminals.hasValidGraph());

    snf::AsyncTaskSequence cycle;
    const auto a = cycle.addTask(std::make_shared<LambdaAsyncTask>(
        [](const snf::AsyncTaskContext&, snf::AsyncTaskContext&) {}));
    const auto b = cycle.addTask(std::make_shared<LambdaAsyncTask>(
        [](const snf::AsyncTaskContext&, snf::AsyncTaskContext&) {}));
    ASSERT_TRUE(cycle.addDependency(a, b));
    ASSERT_TRUE(cycle.addDependency(b, a));
    EXPECT_FALSE(cycle.hasValidGraph());
}
