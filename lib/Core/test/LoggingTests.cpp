#include <gtest/gtest.h>

#include <SNFCore/Application.h>
#include <SNFCore/ConsoleLogSink.h>
#include <SNFCore/LogLevel.h>
#include <SNFCore/LogMessage.h>
#include <SNFCore/Logger.h>
#include <SNFCore/Logging.h>
#include <SNFCore/LogSink.h>
#include <SNFCore/Node.h>
#include <SNFCore/SignalLogSink.h>
#include <SNFCore/ThreadPool.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace snf;
using namespace std::chrono_literals;

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Capturing sink: collects all received messages for assertions.
class CaptureSink : public LogSink
{
public:
    void consume(const LogMessage& msg) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back(msg);
    }

    std::vector<LogMessage> messages() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_messages;
    }

    std::size_t count() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_messages.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.clear();
    }

private:
    mutable std::mutex      m_mutex;
    std::vector<LogMessage> m_messages;
};

class LoggingProbeNode final : public Node
{
public:
    explicit LoggingProbeNode(Node* parent = nullptr)
        : Node(parent)
    {
    }

protected:
    void update() override {}
};

// ── Fixtures ─────────────────────────────────────────────────────────────────

/// Logger tests that do NOT require an Application (standalone usage).
class LoggerStandaloneFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        capture = std::make_shared<CaptureSink>();
        logger  = std::make_unique<Logger>(/*queueCapacity=*/4096);
        logger->addSink(capture);
        logger->start();
    }

    void TearDown() override
    {
        logger->stop();
    }

    std::shared_ptr<CaptureSink> capture;
    std::unique_ptr<Logger>      logger;
};

/// Logger tests that exercise the Application integration path.
class LoggerAppFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        app     = new Application(0, nullptr);
        capture = std::make_shared<CaptureSink>();
        // Replace the default ConsoleLogSink so tests don't flood stderr.
        app->logger().clearSinks();
        app->logger().addSink(capture);
    }

    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

    Application*                 app     = nullptr;
    std::shared_ptr<CaptureSink> capture;
};

TEST(ConsoleLogSinkTest, applicationDefaultSinkPrintsLevelAndTextOnly)
{
    constexpr const char* marker = "default-console-sink-level-text-only";

    testing::internal::CaptureStderr();
    {
        Application app(0, nullptr);
        sInfo() << marker;
        ASSERT_TRUE(app.logger().flush(1000ms));
    }
    const std::string output = testing::internal::GetCapturedStderr();

    const std::string expected = std::string("[INFO] ") + marker;
    ASSERT_NE(output.find(expected), std::string::npos);

    // Default config must keep source metadata off.
    EXPECT_EQ(output.find("LoggingTests.cpp"), std::string::npos);
    EXPECT_EQ(output.find("applicationDefaultSinkPrintsLevelAndTextOnly"), std::string::npos);
}

TEST(ConsoleLogSinkTest, configurableSinkCanPrintAllFields)
{
    constexpr const char* marker = "console-sink-all-fields";

    ConsoleLogSink::Options options;
    options.showTimestamp = true;
    options.showThreadId = true;
    options.showFilePath = true;
    options.showLine = true;
    options.showFunction = true;

    testing::internal::CaptureStderr();
    {
        Logger logger;
        logger.addSink(std::make_shared<ConsoleLogSink>(options));
        logger.start();

        logger.log(LogLevel::Warning, marker, __FILE__, 777, "ConfiguredFunction");
        ASSERT_TRUE(logger.flush(1000ms));
        logger.stop();
    }
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_NE(output.find("[WARNING]"), std::string::npos);
    EXPECT_NE(output.find(marker), std::string::npos);
    EXPECT_NE(output.find("LoggingTests.cpp:777"), std::string::npos);
    EXPECT_NE(output.find("ConfiguredFunction"), std::string::npos);
    EXPECT_NE(output.find("("), std::string::npos);  // thread-id segment
    EXPECT_NE(output.find("T"), std::string::npos);  // timestamp format YYYY-MM-DDTHH:MM:SS.mmm
}

// ── LogLevel utilities ───────────────────────────────────────────────────────

TEST(LogLevelTest, stringRepresentations)
{
    EXPECT_STREQ(logLevelToString(LogLevel::Debug),    "DEBUG");
    EXPECT_STREQ(logLevelToString(LogLevel::Info),     "INFO");
    EXPECT_STREQ(logLevelToString(LogLevel::Warning),  "WARNING");
    EXPECT_STREQ(logLevelToString(LogLevel::Error),    "ERROR");
    EXPECT_STREQ(logLevelToString(LogLevel::Critical), "CRITICAL");
}

TEST(LogLevelTest, ordering)
{
    EXPECT_LT(static_cast<int>(LogLevel::Debug),    static_cast<int>(LogLevel::Info));
    EXPECT_LT(static_cast<int>(LogLevel::Info),     static_cast<int>(LogLevel::Warning));
    EXPECT_LT(static_cast<int>(LogLevel::Warning),  static_cast<int>(LogLevel::Error));
    EXPECT_LT(static_cast<int>(LogLevel::Error),    static_cast<int>(LogLevel::Critical));
}

// ── LogMessage fields ────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, messageCarriesAllFields)
{
    const int expectedLine = __LINE__ + 1;
    logger->log(LogLevel::Warning, "hello", __FILE__, expectedLine, "myFunc");
    ASSERT_TRUE(logger->flush(500ms));

    ASSERT_EQ(capture->count(), 1u);
    const LogMessage msg = capture->messages().front();

    EXPECT_EQ(msg.level,    LogLevel::Warning);
    EXPECT_EQ(msg.text,     "hello");
    EXPECT_EQ(msg.line,     expectedLine);
    EXPECT_STREQ(msg.function, "myFunc");
    EXPECT_NE(msg.sequence, 0u);
    EXPECT_NE(msg.threadId, std::thread::id{});
    // Timestamp must be initialized by Logger::log().
    EXPECT_NE(msg.timestamp, std::chrono::system_clock::time_point{});
}

// ── Level filtering ───────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, messagesAboveMinLevelAreAccepted)
{
    logger->setLevel(LogLevel::Warning);
    logger->log(LogLevel::Warning, "w", __FILE__, __LINE__, __func__);
    logger->log(LogLevel::Critical,"c", __FILE__, __LINE__, __func__);
    ASSERT_TRUE(logger->flush(500ms));
    EXPECT_EQ(capture->count(), 2u);
}

TEST_F(LoggerStandaloneFixture, messagesBelowMinLevelAreDropped)
{
    logger->setLevel(LogLevel::Warning);
    logger->log(LogLevel::Debug, "d", __FILE__, __LINE__, __func__);
    logger->log(LogLevel::Info,  "i", __FILE__, __LINE__, __func__);
    ASSERT_TRUE(logger->flush(500ms));
    EXPECT_EQ(capture->count(), 0u);
    EXPECT_EQ(logger->stats().dropped, 0u); // Dropped at call site, not counted.
}

// ── FIFO ordering ─────────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, messagesDeliveredInFIFOOrder)
{
    constexpr int N = 100;
    for (int i = 0; i < N; ++i) {
        logger->log(LogLevel::Info, std::to_string(i), __FILE__, __LINE__, __func__);
    }
    ASSERT_TRUE(logger->flush(1000ms));

    const auto msgs = capture->messages();
    ASSERT_EQ(msgs.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(msgs[i].text, std::to_string(i));
    }
}

// ── Sequence numbers ──────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, sequenceNumbersAreMonotonicallyIncreasing)
{
    for (int i = 0; i < 10; ++i) {
        logger->log(LogLevel::Debug, "s", __FILE__, __LINE__, __func__);
    }
    ASSERT_TRUE(logger->flush(500ms));

    const auto msgs = capture->messages();
    ASSERT_EQ(msgs.size(), 10u);
    for (std::size_t i = 1; i < msgs.size(); ++i) {
        EXPECT_GT(msgs[i].sequence, msgs[i - 1].sequence);
    }
}

// ── Drop policy ───────────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, droppedCounterIncreasesWhenQueueFull)
{
    // Use a tiny capacity logger.
    auto tinyLogger  = std::make_unique<Logger>(/*queueCapacity=*/4);
    auto tinySink    = std::make_shared<CaptureSink>();

    // Pausing sink: consume() blocks briefly so the queue fills up.
    std::atomic_bool pause{true};
    class PausingSink : public LogSink {
    public:
        std::atomic_bool& pauseFlag;
        explicit PausingSink(std::atomic_bool& f) : pauseFlag(f) {}
        void consume(const LogMessage&) override {
            while (pauseFlag.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    };

    tinyLogger->addSink(std::make_shared<PausingSink>(pause));
    tinyLogger->start();

    // Flood with more messages than the capacity.
    constexpr int kLogsToSend = 20;
    for (int i = 0; i < kLogsToSend; ++i) {
        tinyLogger->log(LogLevel::Debug, "x", __FILE__, __LINE__, __func__);
    }

    // Wait for the queue to fill up by polling dropped counter.
    // Under load (especially in CI containers), this may take longer.
    bool queue_filled = false;
    for (int attempts = 0; attempts < 100; ++attempts) {
        const auto s = tinyLogger->stats();
        if (s.dropped > 0) {
            queue_filled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(queue_filled) << "Queue did not fill up after 100ms";

    const auto stats = tinyLogger->stats();
    EXPECT_GT(stats.dropped, 0u) << "Expected some messages to be dropped due to full queue";
    EXPECT_EQ(stats.enqueued + stats.dropped, static_cast<std::uint64_t>(kLogsToSend))
        << "Total of enqueued and dropped should equal total logs sent";
    EXPECT_LE(stats.enqueued, 5u)
        << "With capacity 4, one extra message may be in-flight in the worker thread";

    // Release the sink and shut down cleanly.
    pause.store(false);
    tinyLogger->stop();
}

// ── Stats ─────────────────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, statsEnqueuedAndProcessedMatch)
{
    constexpr int N = 50;
    for (int i = 0; i < N; ++i) {
        logger->log(LogLevel::Info, "x", __FILE__, __LINE__, __func__);
    }
    ASSERT_TRUE(logger->flush(1000ms));

    const auto s = logger->stats();
    EXPECT_EQ(s.enqueued,  static_cast<std::uint64_t>(N));
    EXPECT_EQ(s.processed, static_cast<std::uint64_t>(N));
    EXPECT_EQ(s.dropped,   0u);
}

// ── Multi-sink fan-out ────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, messagesDeliveredToAllSinks)
{
    auto sink2 = std::make_shared<CaptureSink>();
    logger->addSink(sink2);

    logger->log(LogLevel::Info, "broadcast", __FILE__, __LINE__, __func__);
    ASSERT_TRUE(logger->flush(500ms));

    EXPECT_EQ(capture->count(), 1u);
    EXPECT_EQ(sink2->count(),   1u);
    EXPECT_EQ(capture->messages().front().text, "broadcast");
    EXPECT_EQ(sink2->messages().front().text,   "broadcast");
}

TEST_F(LoggerStandaloneFixture, removeSinkStopsDelivery)
{
    auto sink2 = std::make_shared<CaptureSink>();
    logger->addSink(sink2);
    logger->removeSink(sink2);

    logger->log(LogLevel::Info, "solo", __FILE__, __LINE__, __func__);
    ASSERT_TRUE(logger->flush(500ms));

    EXPECT_EQ(capture->count(), 1u);
    EXPECT_EQ(sink2->count(),   0u);
}

TEST_F(LoggerStandaloneFixture, clearSinksStopsAllDelivery)
{
    logger->clearSinks();
    logger->log(LogLevel::Info, "nobody", __FILE__, __LINE__, __func__);
    ASSERT_TRUE(logger->flush(500ms));
    EXPECT_EQ(capture->count(), 0u);
}

// ── Concurrency: multiple producers ──────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, multipleProducersDontBlock)
{
    constexpr int kThreads  = 8;
    constexpr int kPerThread = 200;

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                logger->log(LogLevel::Debug,
                            "t" + std::to_string(t) + "/" + std::to_string(i),
                            __FILE__, __LINE__, __func__);
            }
        });
    }
    for (auto& th : producers) {
        th.join();
    }

    ASSERT_TRUE(logger->flush(2000ms));

    const auto s = logger->stats();
    // All enqueued messages must have been processed (no drops in an
    // adequately-sized queue).
    EXPECT_EQ(s.enqueued, s.processed);
    EXPECT_EQ(s.dropped,  0u);
    EXPECT_EQ(capture->count(), static_cast<std::size_t>(kThreads * kPerThread));
}

// ── Application integration ───────────────────────────────────────────────────

TEST_F(LoggerAppFixture, macrosSendMessagesToLogger)
{
    sDebug()    << "debug message " << 1;
    sInfo()     << "info message";
    sWarning()  << "warning " << 3.14;
    sError()    << "error";
    sCritical() << "critical";

    ASSERT_TRUE(app->logger().flush(500ms));

    const auto msgs = capture->messages();
    ASSERT_EQ(msgs.size(), 5u);
    EXPECT_EQ(msgs[0].level, LogLevel::Debug);
    EXPECT_EQ(msgs[1].level, LogLevel::Info);
    EXPECT_EQ(msgs[2].level, LogLevel::Warning);
    EXPECT_EQ(msgs[3].level, LogLevel::Error);
    EXPECT_EQ(msgs[4].level, LogLevel::Critical);
}

TEST_F(LoggerAppFixture, macroStreamConcatenatesMultipleValues)
{
    sInfo() << "x=" << 42 << " y=" << 3.14f;
    ASSERT_TRUE(app->logger().flush(500ms));

    ASSERT_EQ(capture->count(), 1u);
    EXPECT_EQ(capture->messages().front().text, "x=42 y=3.14");
}

TEST_F(LoggerAppFixture, macroRecordsCorrectFileAndLine)
{
    const int expectedLine = __LINE__ + 1;
    sWarning() << "check source";
    ASSERT_TRUE(app->logger().flush(500ms));

    ASSERT_EQ(capture->count(), 1u);
    const LogMessage msg = capture->messages().front();
    EXPECT_EQ(msg.line, expectedLine);
    // __FILE__ varies; just verify it is non-empty.
    EXPECT_GT(std::string(msg.file).size(), 0u);
}

TEST_F(LoggerAppFixture, macroRecordsEmittingThreadId)
{
    const std::thread::id mainId = std::this_thread::get_id();
    sInfo() << "thread check";
    ASSERT_TRUE(app->logger().flush(500ms));

    ASSERT_EQ(capture->count(), 1u);
    EXPECT_EQ(capture->messages().front().threadId, mainId);
}

TEST_F(LoggerAppFixture, nodeMovedToThreadPoolLogsFromWorkerThread)
{
    ASSERT_NE(app->threadPool(), nullptr);
    const std::vector<std::thread::id> workerIds = app->threadPool()->workerThreadIds();
    ASSERT_FALSE(workerIds.empty());

    const std::thread::id mainThreadId = std::this_thread::get_id();
    auto* node = new LoggingProbeNode();
    ASSERT_TRUE(node->moveToThreadPool(app->threadPool()));

    const std::thread::id ownerThreadId = node->threadId();
    EXPECT_NE(ownerThreadId, mainThreadId);
    EXPECT_TRUE(std::find(workerIds.begin(), workerIds.end(), ownerThreadId) != workerIds.end());

    constexpr const char* kMarker = "move-to-thread-pool-marker";
    const std::size_t baselineMessageCount = capture->count();
    const std::uint64_t baselineProcessed = app->logger().stats().processed;

    auto executedPromise = std::make_shared<std::promise<std::thread::id>>();
    std::future<std::thread::id> executedFuture = executedPromise->get_future();

    EventLoop* ownerLoop = node->ownerEventLoop();
    ASSERT_NE(ownerLoop, nullptr);
    ownerLoop->post([executedPromise, marker = kMarker]() {
        Application::instance()->logger().log(LogLevel::Info, marker, __FILE__, __LINE__, __func__);
        executedPromise->set_value(std::this_thread::get_id());
    });

    ASSERT_EQ(executedFuture.wait_for(1000ms), std::future_status::ready);
    const std::thread::id executedThreadId = executedFuture.get();
    EXPECT_EQ(executedThreadId, ownerThreadId);

    ASSERT_TRUE(app->logger().flush(1000ms));

    // flush() waits for an empty queue, but sink delivery may still be in
    // progress just after the pop; wait briefly until processed/capture moves.
    const auto deadline = std::chrono::steady_clock::now() + 1000ms;
    while (std::chrono::steady_clock::now() < deadline
           && (app->logger().stats().processed <= baselineProcessed
               || capture->count() <= baselineMessageCount)) {
        std::this_thread::sleep_for(1ms);
    }

    const auto messages = capture->messages();
    ASSERT_GT(messages.size(), baselineMessageCount);
    const LogMessage& lastMessage = messages.back();
    EXPECT_EQ(lastMessage.threadId, ownerThreadId);
    EXPECT_NE(lastMessage.threadId, mainThreadId);

    node->deleteLater();
}

// ── SignalLogSink ─────────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, signalLogSinkEmitsMessages)
{
    auto signalSink = std::make_shared<SignalLogSink>();

    std::vector<LogMessage> received;
    std::mutex              receivedMutex;

    signalSink->messageLogged.connect([&](const LogMessage& msg) {
        std::lock_guard<std::mutex> lock(receivedMutex);
        received.push_back(msg);
    });

    logger->addSink(signalSink);
    logger->log(LogLevel::Info, "signal-test", __FILE__, __LINE__, __func__);
    ASSERT_TRUE(logger->flush(500ms));

    std::lock_guard<std::mutex> lock(receivedMutex);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received.front().text, "signal-test");
}

// ── Shutdown / flush ──────────────────────────────────────────────────────────

TEST_F(LoggerStandaloneFixture, stopDrainsQueueBeforeReturning)
{
    constexpr int N = 300;
    for (int i = 0; i < N; ++i) {
        logger->log(LogLevel::Debug, "drain", __FILE__, __LINE__, __func__);
    }
    // stop() must drain the queue before joining the worker.
    logger->stop();

    // After stop(), all enqueued messages must have been processed.
    const auto s = logger->stats();
    EXPECT_EQ(s.enqueued,  static_cast<std::uint64_t>(N));
    EXPECT_EQ(s.processed, static_cast<std::uint64_t>(N));
}

TEST_F(LoggerAppFixture, applicationDestructorStopsLogger)
{
    // Enqueue some messages, then let TearDown (delete app) stop the logger.
    for (int i = 0; i < 10; ++i) {
        sDebug() << "shutdown-" << i;
    }
    // TearDown will delete the Application; verify no crash/hang occurs.
    // (The test passes if we reach here without blocking.)
}
