#pragma once

/**
 * @file Process.h
 * @brief Non-blocking child-process management with signal-based I/O.
 * @ingroup SNFCore_Process
 */

#include <SNFCore/Connection.h>
#include <SNFCore/ByteArray.h>
#include <SNFCore/Node.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace snf {

class Timer;
class EventLoop;

/**
 * @enum ProcessState
 * @ingroup SNFCore_Process
 * @brief Lifecycle state of a Process instance.
 */
enum class ProcessState {
    NotRunning,
    Starting,
    Running,
};

/**
 * @enum ProcessExitStatus
 * @ingroup SNFCore_Process
 * @brief Indicates how the process terminated.
 */
enum class ProcessExitStatus {
    NormalExit,
    CrashExit,
};

/**
 * @class Process
 * @ingroup SNFCore_Process
 * @brief Non-blocking child process wrapper inspired by QProcess.
 *
 * Process starts child programs asynchronously and exposes all activity through
 * signals. Stdout and stderr can be consumed separately or merged into stdout.
 * Stdin writes are buffered and flushed when the pipe becomes writable.
 *
 * @note Process is available only in native Linux builds. It is excluded from
 *       WebAssembly builds.
 */
class Process final : public Node
{
public:
    explicit Process(Node* parent = nullptr);
    ~Process() override;

    bool start(const std::string& program, const std::vector<std::string>& arguments = {});
    bool waitForFinished(int timeoutMs = 30000);
    void terminate();
    void kill();

    std::size_t write(ByteArray data);
    std::size_t write(const std::string& data);
    void closeWriteChannel();

    ByteArray readAllStandardOutput();
    ByteArray readAllStandardError();

    void setWorkingDirectory(const std::string& workingDirectory);
    std::string workingDirectory() const;

    void setEnvironment(const std::vector<std::string>& environment);
    std::vector<std::string> environment() const;
    void setInheritParentEnvironment(bool inherit);
    bool inheritParentEnvironment() const;

    void setMergedChannels(bool merged);
    bool mergedChannels() const;

    ProcessState state() const;
    int processId() const;

    Signal<>                         started;
    Signal<>                         readyReadStandardOutput;
    Signal<>                         readyReadStandardError;
    Signal<std::size_t>              bytesWritten;
    Signal<int, ProcessExitStatus>   finished;
    Signal<ProcessState>             stateChanged;
    Signal<std::string>              errorOccurred;

protected:
    void update() override;
    void onAboutToMoveToThread(EventLoop* newLoop) override;
    void onMovedToThread(EventLoop* oldLoop) override;

private:
    void startImpl(std::string program, std::vector<std::string> arguments);
    void terminateImpl(bool forceKill);
    std::size_t writeImpl(ByteArray data);

    void setState(ProcessState state);
    void beginExitPolling();
    void pollChildExit();
    void handleChildExit(int waitStatus);

    void registerIOWatches();
    void unregisterIOWatches();
    void refreshStdinWatch();
    void handleStdoutEvents(std::uint32_t nativeEvents);
    void handleStderrEvents(std::uint32_t nativeEvents);
    void handleStdinEvents(std::uint32_t nativeEvents);

    bool readFromPipe(int fd, ByteArray& target, Signal<>& readySignal);
    bool flushWriteQueue();

    void closePipeRead(int& fd);
    void closePipeWrite(int& fd);
    void cleanupProcessResources();

    void emitStarted();
    void emitReadyReadStandardOutput();
    void emitReadyReadStandardError();
    void emitBytesWritten(std::size_t written);
    void emitFinished(int exitCode, ProcessExitStatus exitStatus);
    void emitStateChanged(ProcessState state);
    void emitErrorOccurred(std::string message);

private:
    mutable std::mutex m_mutex;

    ProcessState m_state = ProcessState::NotRunning;
    int m_pid = -1;

    int m_stdinFd = -1;
    int m_stdoutFd = -1;
    int m_stderrFd = -1;

    bool m_stdinWatchRegistered = false;
    bool m_stdoutWatchRegistered = false;
    bool m_stderrWatchRegistered = false;

    bool m_mergedChannels = false;
    bool m_inheritParentEnvironment = true;

    std::string m_program;
    std::vector<std::string> m_arguments;
    std::string m_workingDirectory;
    std::vector<std::string> m_environment;

    ByteArray m_stdoutBuffer;
    ByteArray m_stderrBuffer;

    std::deque<ByteArray> m_writeQueue;

    Timer* m_exitPollTimer = nullptr;
};

}  // namespace snf
