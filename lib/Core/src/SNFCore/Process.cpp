#include "SNFCore/Process.h"

#include "SNFCore/EventLoop.h"
#include "SNFCore/NodePtr.h"
#include "SNFCore/Timer.h"

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>
#include <utility>

extern char** environ;

namespace snf {

namespace {

constexpr std::uint32_t kReadEvents = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
constexpr std::uint32_t kWriteEvents = EPOLLOUT | EPOLLERR | EPOLLHUP;

bool setNonBlocking(int fd)
{
    if (fd < 0) {
        return false;
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void closeFd(int& fd)
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

std::vector<char*> makeArgv(const std::string& program,
                            const std::vector<std::string>& arguments,
                            std::vector<std::string>& storage)
{
    storage.clear();
    storage.reserve(arguments.size() + 1);
    storage.push_back(program);
    storage.insert(storage.end(), arguments.begin(), arguments.end());

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (std::string& entry : storage) {
        argv.push_back(entry.data());
    }
    argv.push_back(nullptr);
    return argv;
}

void applyEnvironment(const std::vector<std::string>& entries)
{
    ::clearenv();
    for (const std::string& entry : entries) {
        const std::size_t split = entry.find('=');
        if (split == std::string::npos) {
            continue;
        }

        const std::string key = entry.substr(0, split);
        const std::string value = entry.substr(split + 1);
        ::setenv(key.c_str(), value.c_str(), 1);
    }
}

}  // namespace

Process::Process(Node* parent) : Node(parent)
{
    m_exitPollTimer = new Timer(this);
    m_exitPollTimer->setInterval(15);
    m_exitPollTimer->timeout.connect(NodePtr<Process>(this), [](Process& self) { self.pollChildExit(); });
}

Process::~Process()
{
    unregisterIOWatches();

    if (m_exitPollTimer) {
        m_exitPollTimer->stop();
    }

    if (m_pid > 0) {
        ::kill(m_pid, SIGKILL);
        int status = 0;
        while (::waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {
        }
        m_pid = -1;
    }

    cleanupProcessResources();
}

bool Process::start(const std::string& program, const std::vector<std::string>& arguments)
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<Process>(this), program, arguments]() {
            if (self) {
                self->startImpl(program, arguments);
            }
        });
        return true;
    }

    if (m_state != ProcessState::NotRunning) {
        emitErrorOccurred("Process is already running");
        return false;
    }

    startImpl(program, arguments);
    return m_state == ProcessState::Running;
}

bool Process::waitForFinished(int timeoutMs)
{
    if (state() == ProcessState::NotRunning) {
        return true;
    }

    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        auto promise = std::make_shared<std::promise<bool>>();
        std::future<bool> result = promise->get_future();

        loop->post([self = NodePtr<Process>(this), promise, timeoutMs]() {
            if (! self) {
                promise->set_value(false);
                return;
            }
            promise->set_value(self->waitForFinished(timeoutMs));
        });

        if (timeoutMs < 0) {
            return result.get();
        }

        const auto timeout = std::chrono::milliseconds(timeoutMs);
        if (result.wait_for(timeout) == std::future_status::ready) {
            return result.get();
        }

        return false;
    }

    const bool waitForever = timeoutMs < 0;
    const auto startedAt = std::chrono::steady_clock::now();

    while (true) {
        if (state() == ProcessState::NotRunning) {
            return true;
        }

        const int pid = processId();
        if (pid <= 0) {
            return state() == ProcessState::NotRunning;
        }

        int waitStatus = 0;
        const pid_t result = ::waitpid(pid, &waitStatus, WNOHANG);
        if (result > 0) {
            handleChildExit(waitStatus);
            return true;
        }

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                setState(ProcessState::NotRunning);
                m_pid = -1;
                cleanupProcessResources();
                return true;
            }

            emitErrorOccurred(std::string("waitpid() failed: ") + std::strerror(errno));
            return false;
        }

        if (! waitForever) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt);
            if (elapsed >= std::chrono::milliseconds(timeoutMs)) {
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void Process::terminate()
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<Process>(this)]() {
            if (self) {
                self->terminateImpl(false);
            }
        });
        return;
    }

    terminateImpl(false);
}

void Process::kill()
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<Process>(this)]() {
            if (self) {
                self->terminateImpl(true);
            }
        });
        return;
    }

    terminateImpl(true);
}

std::size_t Process::write(ByteArray data)
{
    if (data.remainingSize() == 0) {
        return 0;
    }

    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        const bool canQueue = [this]() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_state == ProcessState::Running && m_stdinFd >= 0;
        }();

        if (! canQueue) {
            return 0;
        }

        const std::size_t accepted = data.remainingSize();
        loop->post([self = NodePtr<Process>(this), pending = std::move(data)]() mutable {
            if (self) {
                self->writeImpl(std::move(pending));
            }
        });
        return accepted;
    }

    return writeImpl(std::move(data));
}

std::size_t Process::write(const std::string& data)
{
    return write(ByteArray(data));
}

void Process::closeWriteChannel()
{
    if (EventLoop* loop = ownerEventLoop(); loop && ! loop->isInThisThread()) {
        loop->post([self = NodePtr<Process>(this)]() {
            if (self) {
                self->closeWriteChannel();
            }
        });
        return;
    }

    closePipeWrite(m_stdinFd);
}

ByteArray Process::readAllStandardOutput()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ByteArray output;
    std::swap(m_stdoutBuffer, output);
    return output;
}

ByteArray Process::readAllStandardError()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ByteArray output;
    std::swap(m_stderrBuffer, output);
    return output;
}

void Process::setWorkingDirectory(const std::string& workingDirectory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != ProcessState::NotRunning) {
        return;
    }
    m_workingDirectory = workingDirectory;
}

std::string Process::workingDirectory() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_workingDirectory;
}

void Process::setEnvironment(const std::vector<std::string>& environment)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != ProcessState::NotRunning) {
        return;
    }
    m_environment = environment;
    m_inheritParentEnvironment = false;
}

std::vector<std::string> Process::environment() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_environment;
}

void Process::setInheritParentEnvironment(bool inherit)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != ProcessState::NotRunning) {
        return;
    }
    m_inheritParentEnvironment = inherit;
}

bool Process::inheritParentEnvironment() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inheritParentEnvironment;
}

void Process::setMergedChannels(bool merged)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != ProcessState::NotRunning) {
        return;
    }
    m_mergedChannels = merged;
}

bool Process::mergedChannels() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mergedChannels;
}

ProcessState Process::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

int Process::processId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pid;
}

void Process::update() {}

void Process::onAboutToMoveToThread(EventLoop* /*newLoop*/)
{
    unregisterIOWatches();
}

void Process::onMovedToThread(EventLoop* /*oldLoop*/)
{
    registerIOWatches();
}

void Process::startImpl(std::string program, std::vector<std::string> arguments)
{
    if (program.empty()) {
        emitErrorOccurred("Program path is empty");
        return;
    }

    setState(ProcessState::Starting);

    int stdinPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};

    if (::pipe(stdinPipe) != 0 || ::pipe(stdoutPipe) != 0) {
        closeFd(stdinPipe[0]);
        closeFd(stdinPipe[1]);
        closeFd(stdoutPipe[0]);
        closeFd(stdoutPipe[1]);
        emitErrorOccurred(std::string("Failed to create process pipes: ") + std::strerror(errno));
        setState(ProcessState::NotRunning);
        return;
    }

    if (! m_mergedChannels && ::pipe(stderrPipe) != 0) {
        closeFd(stdinPipe[0]);
        closeFd(stdinPipe[1]);
        closeFd(stdoutPipe[0]);
        closeFd(stdoutPipe[1]);
        closeFd(stderrPipe[0]);
        closeFd(stderrPipe[1]);
        emitErrorOccurred(std::string("Failed to create stderr pipe: ") + std::strerror(errno));
        setState(ProcessState::NotRunning);
        return;
    }

    std::vector<std::string> argvStorage;
    std::vector<char*> argv = makeArgv(program, arguments, argvStorage);

    const std::string workingDirectory = m_workingDirectory;
    const bool inheritParentEnvironment = m_inheritParentEnvironment;
    const std::vector<std::string> environment = m_environment;

    const pid_t pid = ::fork();
    if (pid < 0) {
        closeFd(stdinPipe[0]);
        closeFd(stdinPipe[1]);
        closeFd(stdoutPipe[0]);
        closeFd(stdoutPipe[1]);
        closeFd(stderrPipe[0]);
        closeFd(stderrPipe[1]);
        emitErrorOccurred(std::string("fork() failed: ") + std::strerror(errno));
        setState(ProcessState::NotRunning);
        return;
    }

    if (pid == 0) {
        if (! workingDirectory.empty()) {
            ::chdir(workingDirectory.c_str());
        }

        ::dup2(stdinPipe[0], STDIN_FILENO);
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        if (m_mergedChannels) {
            ::dup2(stdoutPipe[1], STDERR_FILENO);
        } else {
            ::dup2(stderrPipe[1], STDERR_FILENO);
        }

        closeFd(stdinPipe[0]);
        closeFd(stdinPipe[1]);
        closeFd(stdoutPipe[0]);
        closeFd(stdoutPipe[1]);
        closeFd(stderrPipe[0]);
        closeFd(stderrPipe[1]);

        if (! inheritParentEnvironment) {
            applyEnvironment(environment);
        }

        ::execvp(program.c_str(), argv.data());
        _exit(127);
    }

    closeFd(stdinPipe[0]);
    closeFd(stdoutPipe[1]);
    closeFd(stderrPipe[1]);

    if (! setNonBlocking(stdinPipe[1]) || ! setNonBlocking(stdoutPipe[0])
        || (! m_mergedChannels && ! setNonBlocking(stderrPipe[0]))) {
        closeFd(stdinPipe[1]);
        closeFd(stdoutPipe[0]);
        closeFd(stderrPipe[0]);
        ::kill(pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        emitErrorOccurred("Failed to configure non-blocking pipes");
        setState(ProcessState::NotRunning);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_program = std::move(program);
        m_arguments = std::move(arguments);
        m_pid = static_cast<int>(pid);
        m_stdinFd = stdinPipe[1];
        m_stdoutFd = stdoutPipe[0];
        m_stderrFd = m_mergedChannels ? -1 : stderrPipe[0];
        m_stdoutBuffer.clear();
        m_stderrBuffer.clear();
        m_writeQueue.clear();
    }

    registerIOWatches();
    beginExitPolling();
    setState(ProcessState::Running);
    emitStarted();
}

void Process::terminateImpl(bool forceKill)
{
    if (m_state != ProcessState::Running) {
        return;
    }

    const int pid = m_pid;
    if (pid <= 0) {
        return;
    }

    const int signalToSend = forceKill ? SIGKILL : SIGTERM;
    if (::kill(pid, signalToSend) != 0 && errno != ESRCH) {
        emitErrorOccurred(std::string("kill() failed: ") + std::strerror(errno));
    }
}

std::size_t Process::writeImpl(ByteArray data)
{
    if (data.remainingSize() == 0) {
        return 0;
    }

    const std::size_t accepted = data.remainingSize();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != ProcessState::Running || m_stdinFd < 0) {
            return 0;
        }
        m_writeQueue.push_back(std::move(data));
    }

    flushWriteQueue();
    refreshStdinWatch();
    return accepted;
}

void Process::setState(ProcessState state)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != state) {
            m_state = state;
            changed = true;
        }
    }

    if (changed) {
        emitStateChanged(state);
    }
}

void Process::beginExitPolling()
{
    if (! m_exitPollTimer) {
        return;
    }
    m_exitPollTimer->start();
}

void Process::pollChildExit()
{
    const int pid = m_pid;
    if (pid <= 0) {
        if (m_exitPollTimer) {
            m_exitPollTimer->stop();
        }
        return;
    }

    while (true) {
        int waitStatus = 0;
        const pid_t result = ::waitpid(pid, &waitStatus, WNOHANG);
        if (result == 0) {
            return;
        }

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == ECHILD) {
                if (m_exitPollTimer) {
                    m_exitPollTimer->stop();
                }
                setState(ProcessState::NotRunning);
                m_pid = -1;
                return;
            }

            emitErrorOccurred(std::string("waitpid() failed: ") + std::strerror(errno));
            return;
        }

        handleChildExit(waitStatus);
        return;
    }
}

void Process::handleChildExit(int waitStatus)
{
    if (m_exitPollTimer) {
        m_exitPollTimer->stop();
    }

    closePipeWrite(m_stdinFd);

    if (m_stdoutFd >= 0) {
        readFromPipe(m_stdoutFd, m_stdoutBuffer, readyReadStandardOutput);
        closePipeRead(m_stdoutFd);
    }

    if (m_stderrFd >= 0) {
        readFromPipe(m_stderrFd, m_stderrBuffer, readyReadStandardError);
        closePipeRead(m_stderrFd);
    }

    ProcessExitStatus exitStatus = ProcessExitStatus::CrashExit;
    int exitCode = -1;

    if (WIFEXITED(waitStatus)) {
        exitStatus = ProcessExitStatus::NormalExit;
        exitCode = WEXITSTATUS(waitStatus);
    }

    m_pid = -1;
    setState(ProcessState::NotRunning);
    emitFinished(exitCode, exitStatus);
}

void Process::registerIOWatches()
{
    EventLoop* loop = ownerEventLoop();
    if (! loop) {
        return;
    }

    if (m_stdoutFd >= 0 && ! m_stdoutWatchRegistered) {
        loop->registerIO(m_stdoutFd,
                         kReadEvents,
                         [self = NodePtr<Process>(this)](std::uint32_t events) {
                             if (self) {
                                 self->handleStdoutEvents(events);
                             }
                         });
        m_stdoutWatchRegistered = true;
    }

    if (m_stderrFd >= 0 && ! m_stderrWatchRegistered) {
        loop->registerIO(m_stderrFd,
                         kReadEvents,
                         [self = NodePtr<Process>(this)](std::uint32_t events) {
                             if (self) {
                                 self->handleStderrEvents(events);
                             }
                         });
        m_stderrWatchRegistered = true;
    }

    refreshStdinWatch();
}

void Process::unregisterIOWatches()
{
    EventLoop* loop = ownerEventLoop();
    if (! loop) {
        return;
    }

    if (m_stdoutWatchRegistered && m_stdoutFd >= 0) {
        loop->unregisterIO(m_stdoutFd);
    }
    if (m_stderrWatchRegistered && m_stderrFd >= 0) {
        loop->unregisterIO(m_stderrFd);
    }
    if (m_stdinWatchRegistered && m_stdinFd >= 0) {
        loop->unregisterIO(m_stdinFd);
    }

    m_stdoutWatchRegistered = false;
    m_stderrWatchRegistered = false;
    m_stdinWatchRegistered = false;
}

void Process::refreshStdinWatch()
{
    EventLoop* loop = ownerEventLoop();
    if (! loop) {
        return;
    }

    if (m_stdinFd < 0) {
        if (m_stdinWatchRegistered) {
            m_stdinWatchRegistered = false;
        }
        return;
    }

    const bool hasPendingWrites = ! m_writeQueue.empty();
    const std::uint32_t events = hasPendingWrites ? kWriteEvents : (EPOLLERR | EPOLLHUP);

    if (! m_stdinWatchRegistered) {
        loop->registerIO(m_stdinFd,
                         events,
                         [self = NodePtr<Process>(this)](std::uint32_t nativeEvents) {
                             if (self) {
                                 self->handleStdinEvents(nativeEvents);
                             }
                         });
        m_stdinWatchRegistered = true;
        return;
    }

    loop->modifyIO(m_stdinFd, events);
}

void Process::handleStdoutEvents(std::uint32_t nativeEvents)
{
    const bool stillOpen = readFromPipe(m_stdoutFd, m_stdoutBuffer, readyReadStandardOutput);
    if (! stillOpen && (nativeEvents & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) != 0U) {
        closePipeRead(m_stdoutFd);
    }
}

void Process::handleStderrEvents(std::uint32_t nativeEvents)
{
    const bool stillOpen = readFromPipe(m_stderrFd, m_stderrBuffer, readyReadStandardError);
    if (! stillOpen && (nativeEvents & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) != 0U) {
        closePipeRead(m_stderrFd);
    }
}

void Process::handleStdinEvents(std::uint32_t nativeEvents)
{
    if ((nativeEvents & EPOLLOUT) != 0U) {
        flushWriteQueue();
    }

    if ((nativeEvents & (EPOLLERR | EPOLLHUP)) != 0U) {
        closePipeWrite(m_stdinFd);
    }

    refreshStdinWatch();
}

bool Process::readFromPipe(int fd, ByteArray& target, Signal<>& readySignal)
{
    if (fd < 0) {
        return false;
    }

    bool appended = false;
    std::uint8_t buffer[4096];

    while (true) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                target.append(buffer, static_cast<std::size_t>(count));
            }
            appended = true;
            continue;
        }

        if (count == 0) {
            if (appended) {
                readySignal.emit();
            }
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (appended) {
                readySignal.emit();
            }
            return true;
        }

        emitErrorOccurred(std::string("read() failed: ") + std::strerror(errno));
        return false;
    }
}

bool Process::flushWriteQueue()
{
    if (m_stdinFd < 0) {
        return false;
    }

    while (! m_writeQueue.empty()) {
        ByteArray& front = m_writeQueue.front();
        const std::byte* data = front.remainingData();
        const std::size_t remaining = front.remainingSize();

        const ssize_t written = ::write(m_stdinFd, data, remaining);
        if (written > 0) {
            front.advance(static_cast<std::size_t>(written));
            emitBytesWritten(static_cast<std::size_t>(written));

            if (front.fullyConsumed()) {
                m_writeQueue.pop_front();
            }
            continue;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false;
        }

        if (written < 0 && (errno == EPIPE || errno == ECONNRESET)) {
            closePipeWrite(m_stdinFd);
            return false;
        }

        emitErrorOccurred(std::string("write() failed: ") + std::strerror(errno));
        closePipeWrite(m_stdinFd);
        return false;
    }

    return true;
}

void Process::closePipeRead(int& fd)
{
    EventLoop* loop = ownerEventLoop();
    if (loop && fd >= 0) {
        loop->unregisterIO(fd);
    }

    if (fd == m_stdoutFd) {
        m_stdoutWatchRegistered = false;
    }
    if (fd == m_stderrFd) {
        m_stderrWatchRegistered = false;
    }

    closeFd(fd);
}

void Process::closePipeWrite(int& fd)
{
    EventLoop* loop = ownerEventLoop();
    if (loop && fd >= 0) {
        loop->unregisterIO(fd);
    }

    if (fd == m_stdinFd) {
        m_stdinWatchRegistered = false;
        m_writeQueue.clear();
    }

    closeFd(fd);
}

void Process::cleanupProcessResources()
{
    closePipeWrite(m_stdinFd);
    closePipeRead(m_stdoutFd);
    closePipeRead(m_stderrFd);
}

void Process::emitStarted()
{
    started.emit();
}

void Process::emitReadyReadStandardOutput()
{
    readyReadStandardOutput.emit();
}

void Process::emitReadyReadStandardError()
{
    readyReadStandardError.emit();
}

void Process::emitBytesWritten(std::size_t written)
{
    bytesWritten.emit(written);
}

void Process::emitFinished(int exitCode, ProcessExitStatus exitStatus)
{
    finished.emit(exitCode, exitStatus);
}

void Process::emitStateChanged(ProcessState state)
{
    stateChanged.emit(state);
}

void Process::emitErrorOccurred(std::string message)
{
    errorOccurred.emit(std::move(message));
}

}  // namespace snf
