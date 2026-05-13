#include <gtest/gtest.h>

#include "SNFCore/Application.h"
#include "SNFCore/EventLoop.h"
#include "SNFCore/Process.h"
#include "SNFCore/Timer.h"

#include <chrono>
#include <string>
#include <thread>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class ProcessFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }

    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

protected:
    bool runWithTimeout(std::chrono::milliseconds timeout)
    {
        bool timedOut = false;

        Timer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        timeoutTimer.timeout.connect([&]() {
            timedOut = true;
            if (EventLoop* loop = app->getOrCreateCurrentThreadEventLoop()) {
                loop->stop();
            }
        });
        timeoutTimer.start(timeout);

        app->run();
        return !timedOut;
    }

    Application* app = nullptr;
};

}  // namespace

TEST_F(ProcessFixture, capturesStandardOutput)
{
    Process process;

    std::string output;
    int exitCode = -1;
    ProcessExitStatus exitStatus = ProcessExitStatus::CrashExit;

    process.readyReadStandardOutput.connect([&]() {
        output += process.readAllStandardOutput().toString();
    });

    process.finished.connect([&](int code, ProcessExitStatus status) {
        exitCode = code;
        exitStatus = status;
        if (EventLoop* loop = process.ownerEventLoop()) {
            loop->stop();
        }
    });

    ASSERT_TRUE(process.start("/bin/sh", {"-c", "printf hello"}));
    ASSERT_TRUE(runWithTimeout(2s));

    EXPECT_EQ(output, "hello");
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(exitStatus, ProcessExitStatus::NormalExit);
}

TEST_F(ProcessFixture, capturesStandardError)
{
    Process process;

    std::string stdoutData;
    std::string stderrData;

    process.readyReadStandardOutput.connect([&]() {
        stdoutData += process.readAllStandardOutput().toString();
    });

    process.readyReadStandardError.connect([&]() {
        stderrData += process.readAllStandardError().toString();
    });

    process.finished.connect([&](int, ProcessExitStatus) {
        if (EventLoop* loop = process.ownerEventLoop()) {
            loop->stop();
        }
    });

    ASSERT_TRUE(process.start("/bin/sh", {"-c", "printf errout 1>&2"}));
    ASSERT_TRUE(runWithTimeout(2s));

    EXPECT_TRUE(stdoutData.empty());
    EXPECT_EQ(stderrData, "errout");
}

TEST_F(ProcessFixture, mergesChannelsIntoStdout)
{
    Process process;
    process.setMergedChannels(true);

    std::string merged;
    int stderrSignalCount = 0;

    process.readyReadStandardOutput.connect([&]() {
        merged += process.readAllStandardOutput().toString();
    });

    process.readyReadStandardError.connect([&]() { ++stderrSignalCount; });

    process.finished.connect([&](int, ProcessExitStatus) {
        if (EventLoop* loop = process.ownerEventLoop()) {
            loop->stop();
        }
    });

    ASSERT_TRUE(process.start("/bin/sh", {"-c", "printf out; printf err 1>&2"}));
    ASSERT_TRUE(runWithTimeout(2s));

    EXPECT_EQ(merged, "outerr");
    EXPECT_EQ(stderrSignalCount, 0);
    EXPECT_TRUE(process.readAllStandardError().empty());
}

TEST_F(ProcessFixture, writesToStandardInputNonBlocking)
{
    Process process;

    std::string output;
    std::size_t bytesWrittenTotal = 0;

    process.started.connect([&]() {
        bytesWrittenTotal += process.write("ping\n");
        process.closeWriteChannel();
    });

    process.readyReadStandardOutput.connect([&]() {
            output += process.readAllStandardOutput().toString();
    });

    process.bytesWritten.connect([&](std::size_t) {});

    process.finished.connect([&](int, ProcessExitStatus) {
        if (EventLoop* loop = process.ownerEventLoop()) {
            loop->stop();
        }
    });

    ASSERT_TRUE(process.start("/bin/cat"));
    ASSERT_TRUE(runWithTimeout(2s));

    EXPECT_GE(bytesWrittenTotal, 5U);
    EXPECT_EQ(output, "ping\n");
}

TEST_F(ProcessFixture, terminateStopsLongRunningProcess)
{
    Process process;

    int exitCode = 0;
    ProcessExitStatus exitStatus = ProcessExitStatus::NormalExit;

    process.started.connect([&]() {
        Timer::singleShot(100ms, [&]() { process.terminate(); }, &process);
    });

    process.finished.connect([&](int code, ProcessExitStatus status) {
        exitCode = code;
        exitStatus = status;
        if (EventLoop* loop = process.ownerEventLoop()) {
            loop->stop();
        }
    });

    ASSERT_TRUE(process.start("/bin/sh", {"-c", "sleep 10"}));
    ASSERT_TRUE(runWithTimeout(3s));

    EXPECT_NE(exitCode, 0);
    EXPECT_EQ(exitStatus, ProcessExitStatus::CrashExit);
}

TEST_F(ProcessFixture, startFromForeignThreadPostsToOwnerLoop)
{
    Process process;

    std::thread::id callbackThread;
    std::string output;

    process.readyReadStandardOutput.connect([&]() {
        output += process.readAllStandardOutput().toString();
    });

    process.finished.connect([&](int, ProcessExitStatus) {
        callbackThread = std::this_thread::get_id();
        if (EventLoop* loop = process.ownerEventLoop()) {
            loop->stop();
        }
    });

    std::thread worker([&]() {
        EXPECT_TRUE(process.start("/bin/sh", {"-c", "printf cross"}));
    });
    worker.join();

    ASSERT_TRUE(runWithTimeout(2s));

    EXPECT_EQ(output, "cross");
    EXPECT_EQ(callbackThread, process.ownerThreadId());
}

TEST_F(ProcessFixture, waitForFinishedReturnsTrueOnNaturalExit)
{
    Process process;

    ASSERT_TRUE(process.start("/bin/sh", {"-c", "sleep 0.05"}));
    EXPECT_TRUE(process.waitForFinished(2000));
    EXPECT_EQ(process.state(), ProcessState::NotRunning);
}

TEST_F(ProcessFixture, waitForFinishedReturnsFalseOnTimeout)
{
    Process process;

    ASSERT_TRUE(process.start("/bin/sh", {"-c", "sleep 1"}));
    EXPECT_FALSE(process.waitForFinished(20));

    process.kill();
    EXPECT_TRUE(process.waitForFinished(2000));
}

TEST_F(ProcessFixture, waitForFinishedCanBeCalledFromForeignThread)
{
    Process process;

    ASSERT_TRUE(process.start("/bin/sh", {"-c", "sleep 0.05"}));

    bool result = false;
    std::thread worker([&]() { result = process.waitForFinished(2000); });

    // The owner loop must run to process the marshaled wait request.
    Timer::singleShot(300ms, [&]() {
        if (EventLoop* loop = process.ownerEventLoop()) {
            loop->stop();
        }
    }, &process);

    app->run();
    worker.join();

    EXPECT_TRUE(result);
    EXPECT_EQ(process.state(), ProcessState::NotRunning);
}
