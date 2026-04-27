/**
 * @file EpollPollerStub.cpp
 * @brief WebAssembly/Emscripten stub for EpollPoller.
 *
 * In a WebAssembly context there is no epoll, eventfd, or any blocking
 * kernel I/O multiplexer.  The browser drives its own event loop via
 * emscripten_set_main_loop_arg() (see SNFWidgets::WebApplicationNode).
 *
 * This stub fulfils the EpollPoller contract so that SNFCore compiles
 * cleanly with Emscripten:
 *   - addOrUpdate(), remove(), wakeUp(), and drainWakeupFd() are no-ops.
 *   - wait() returns an empty event list immediately regardless of timeout.
 *
 * Raw file-descriptor I/O registered on an EventLoop (registerIO /
 * modifyIO / unregisterIO) is accepted at the API level but has no
 * effect in web builds.  The intended integration point for per-frame
 * event processing is EventLoop::runPendingWork(), called from within
 * the Emscripten main-loop callback.
 */

#include "SNFCore/EpollPoller.h"

namespace snf {

EpollPoller::EpollPoller() = default;

EpollPoller::~EpollPoller() = default;

void EpollPoller::addOrUpdate(int /*fd*/, std::uint32_t /*events*/) {}

void EpollPoller::remove(int /*fd*/) {}

void EpollPoller::wakeUp() {}

std::vector<EpollPoller::Event> EpollPoller::wait(int /*timeoutMs*/)
{
    return {};
}

void EpollPoller::drainWakeupFd() {}

}  // namespace snf
