#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/HostAddress.h>
#include <SNFWebSocket/WebSocket.h>

#include <chrono>
#include <iostream>
#include <string>

using namespace snf;
using namespace std::chrono_literals;

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (! condition) {
        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }
}

void testEventLoopPendingWork()
{
    Application app(0, nullptr);
    EventLoop* loop = app.getOrCreateCurrentThreadEventLoop();
    expect(loop != nullptr, "Application creates a main EventLoop");

    bool postedTaskRan = false;
    loop->post([&postedTaskRan]() { postedTaskRan = true; });
    loop->runPendingWork();
    expect(postedTaskRan, "runPendingWork executes posted tasks");

    bool timerFired = false;
    Timer timer;
    timer.setSingleShot(true);
    timer.timeout.connect([&timerFired]() { timerFired = true; });
    timer.start(0ms);
    loop->runPendingWork();
    expect(timerFired, "runPendingWork dispatches due timers");

    bool ioCallbackRan = false;
    loop->registerIO(42, 1u, [&ioCallbackRan](std::uint32_t) { ioCallbackRan = true; });
    loop->runPendingWork();
    loop->unregisterIO(42);
    expect(! ioCallbackRan, "WebAssembly EventLoop accepts fd watches without polling epoll");
}

void testHostAddress()
{
    expect(HostAddress::LocalHost.toString() == "127.0.0.1", "HostAddress localhost toString");
    expect(HostAddress::LocalHost.isValid(), "HostAddress validates IPv4 localhost");
    expect(HostAddress::LocalHostIPv6.isValid(), "HostAddress validates IPv6 localhost");
    expect(HostAddress::isValidHost("example.com"), "HostAddress validates hostnames");
    expect(! HostAddress::isValidHost("bad host.example"), "HostAddress rejects invalid hostnames");
}

void testWebSocketApiSurface()
{
    Application app(0, nullptr);
    WebSocket socket;

    bool errorEmitted = false;
    std::string errorMessage;
    socket.errorOccurred.connect([&](const std::string& error) {
        errorEmitted = true;
        errorMessage = error;
    });

    socket.connectToHost(HostAddress::AnyIPv4, 8765, "/");

    expect(errorEmitted, "Emscripten WebSocket rejects wildcard connect address");
    expect(socket.state() == WebSocketState::Error, "Rejected WebSocket enters Error state");
    expect(errorMessage.find("wildcard") != std::string::npos, "Wildcard WebSocket error is descriptive");
    expect(! socket.isOpen(), "Rejected WebSocket is not open");
}

}  // namespace

int main()
{
    testEventLoopPendingWork();
    testHostAddress();
    testWebSocketApiSurface();

    if (failures != 0) {
        std::cerr << failures << " WebAssembly smoke assertion(s) failed\n";
        return 1;
    }

    std::cout << "WebAssembly smoke tests passed\n";
    return 0;
}
