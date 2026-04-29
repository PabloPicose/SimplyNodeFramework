#include "SNFCore/Application.h"

#if !defined(SNF_PLATFORM_WEB)
#include <unistd.h>
#endif

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "CommandLineParser.h"
#include "EventLoop.h"
#include "Node.h"
#include "NodePtr.h"

namespace snf {

Application* Application::m_instance = nullptr;

Application::Application(int argc, char** argv) : m_threadId(std::this_thread::get_id()), m_argc(argc), m_argv(argv)
{
    if (m_instance) {
        throw std::runtime_error("Only one instance of Application is allowed");
    } else {
        m_instance = this;
    }
    // Eagerly create the main-thread EventLoop so it is always available.
    getOrCreateCurrentThreadEventLoop();
}

Application::~Application()
{
    // Clearing the event-loops map destroys each EventLoop, whose destructor
    // deletes its root nodes (and their subtrees) in the correct owner thread.
    m_eventLoops.clear();
    m_instance = nullptr;
}

std::string Application::getFullExecutablePath() const
{
#if defined(SNF_PLATFORM_WEB)
    // No /proc filesystem in WebAssembly.
    return {};
#else
    constexpr size_t PATH_MAX = 4096;
    // linux
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    return (count != -1) ? std::string(result, count) : std::string();
#endif
}

Application* Application::instance() { return m_instance; }

std::thread::id Application::threadId() { return std::this_thread::get_id(); }

void Application::loopOnce()
{
    if (EventLoop* loop = mainEventLoop()) {
        loop->runPendingWork();
    }
}

int Application::run()
{
    if (EventLoop* loop = mainEventLoop()) {
        loop->run();
    }
    return 0;
}

void Application::quit()
{
    m_quit = true;
    std::lock_guard<std::mutex> lock(m_eventLoopsMutex);
    for (auto& [threadId, loop] : m_eventLoops) {
        loop->stop();
    }
}

std::size_t Application::getRootNodesCount() const
{
    if (EventLoop* loop = mainEventLoop()) {
        return loop->getRootNodesCount();
    }
    return 0;
}

Node* Application::getRootNode(const std::size_t index) const
{
    if (EventLoop* loop = mainEventLoop()) {
        return loop->getRootNode(index);
    }
    return nullptr;
}

bool Application::isNodeAlive(Node* node, std::uint64_t generation) const
{
    std::lock_guard<std::mutex> lock(m_aliveNodesMutex);
    auto it = m_aliveNodes.find(node);
    if (it == m_aliveNodes.cend()) {
        return false;
    }
    return it->second.generation == generation;
}

bool Application::isNodeMarkedToDelete(Node* node, std::uint64_t generation) const
{
    std::lock_guard<std::mutex> lock(m_aliveNodesMutex);
    auto it = m_aliveNodes.find(node);
    if (it == m_aliveNodes.cend()) {
        return true;
    }
    if (it->second.generation != generation) {
        return true;
    }
    return it->second.markedForDelete;
}

size_t Application::getRootNodesToDeleteCount() const
{
    if (EventLoop* loop = mainEventLoop()) {
        return loop->getRootNodesToDeleteCount();
    }
    return 0;
}

size_t Application::getAliveNodesCount() const
{
    std::lock_guard<std::mutex> lock(m_aliveNodesMutex);
    return m_aliveNodes.size();
}

size_t Application::getAliveNodesToDeleteCount() const
{
    std::lock_guard<std::mutex> lock(m_aliveNodesMutex);
    size_t count = 0;
    for (const auto& [node, entry] : m_aliveNodes) {
        if (entry.markedForDelete) {
            count++;
        }
    }
    return count;
}

void Application::setApplicationVersion(const std::string& version) { m_version = version; }

CommandLineParser& Application::getCommandLineParser()
{
    if (!m_commandLineParser) {
        m_commandLineParser = std::make_unique<CommandLineParser>();
    }
    return *m_commandLineParser;
}

std::list<std::string> Application::getArguments() const
{
    std::list<std::string> output;
    for (int i = 0; i < m_argc; ++i) {
        std::string arg = m_argv[i];
        output.push_back(arg);
    }
    return output;
}

EventLoop* Application::mainEventLoop() const
{
    std::lock_guard<std::mutex> lock(m_eventLoopsMutex);
    auto it = m_eventLoops.find(m_threadId);
    if (it == m_eventLoops.end()) {
        return nullptr;
    }
    return it->second.get();
}

EventLoop* Application::getOrCreateCurrentThreadEventLoop()
{
    const std::thread::id currentThreadId = Application::threadId();
    std::lock_guard<std::mutex> lock(m_eventLoopsMutex);
    auto it = m_eventLoops.find(currentThreadId);
    if (it != m_eventLoops.end()) {
        return it->second.get();
    }

    auto [newIt, inserted] = m_eventLoops.emplace(currentThreadId, std::make_unique<EventLoop>());
    if (! inserted) {
        throw std::runtime_error("Could not create EventLoop for thread");
    }
    return newIt->second.get();
}

EventLoop* Application::getEventLoopByThreadId(std::thread::id threadId) const
{
    std::lock_guard<std::mutex> lock(m_eventLoopsMutex);
    auto it = m_eventLoops.find(threadId);
    if (it == m_eventLoops.end()) {
        return nullptr;
    }
    return it->second.get();
}

size_t Application::getEventLoopCount() const
{
    std::lock_guard<std::mutex> lock(m_eventLoopsMutex);
    return m_eventLoops.size();
}

void Application::pushRootNodeDeleteLater(Node* node)
{
    if (EventLoop* loop = mainEventLoop()) {
        loop->enqueueDelete(node);
    }
}

void Application::registerAliveNode(Node* node)
{
    std::lock_guard<std::mutex> lock(m_aliveNodesMutex);
    m_aliveNodes[node] = NodeEntry{node->generation(), false};
}

void Application::markToDelete(Node* node)
{
    std::lock_guard<std::mutex> lock(m_aliveNodesMutex);
    auto it = m_aliveNodes.find(node);
    if (it == m_aliveNodes.cend()) {
        throw std::runtime_error("Node not found in the alive nodes");
    }
    it->second.markedForDelete = true;
}

void Application::unregisterAliveNode(Node* node)
{
    std::lock_guard<std::mutex> lock(m_aliveNodesMutex);
    if (m_aliveNodes.find(node) == m_aliveNodes.cend()) {
        throw std::runtime_error("Node not found in the alive nodes");
    }
    m_aliveNodes.erase(node);
}

bool Application::allEventLoopsIdle() const
{
    std::lock_guard<std::mutex> lock(m_eventLoopsMutex);
    for (const auto& [threadId, loop] : m_eventLoops) {
        if (loop->hasPendingWork()) {
            return false;
        }
    }
    return true;
}

}  // namespace snf
