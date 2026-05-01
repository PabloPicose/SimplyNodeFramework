#include <SNFCore/Application.h>
#include <SNFCore/File.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/TempFile.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/HostAddress.h>
#include <SNFWebSocket/WebSocket.h>

#include <sys/socket.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

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

    std::vector<sockaddr_storage> addresses;
    std::string error;

    expect(HostAddress::LocalHost.resolve(80, HostResolveMode::Connect, addresses, error),
           "WebAssembly HostAddress resolves numeric IPv4 locally");
    expect(! addresses.empty(), "WebAssembly numeric IPv4 resolve returns a candidate");

    addresses.clear();
    error.clear();
    expect(HostAddress::LocalHostIPv6.resolve(80, HostResolveMode::Connect, addresses, error),
           "WebAssembly HostAddress resolves numeric IPv6 locally");
    expect(! addresses.empty(), "WebAssembly numeric IPv6 resolve returns a candidate");

    addresses.clear();
    error.clear();
    expect(! HostAddress("example.com").resolve(80, HostResolveMode::Connect, addresses, error),
           "WebAssembly HostAddress does not use native DNS resolution for hostnames");
    expect(addresses.empty(), "WebAssembly hostname resolve does not return native socket candidates");
    expect(error.find("WebAssembly") != std::string::npos, "WebAssembly hostname resolve error is explicit");
}

void testFileApiSurface()
{
    const std::string path = "/snf_wasm_file_test.bin";
    File::remove(path);

    expect(! File::exists(path), "WebAssembly File starts with a missing virtual path");
    expect(File::writeAll(path, ByteArray(std::string("wasm"))), "WebAssembly File writes virtual FS data");

    const auto staticContent = File::readAll(path);
    expect(staticContent.has_value(), "WebAssembly File reads virtual FS data");
    expect(staticContent.has_value() && staticContent->size() == 4, "WebAssembly File read size is correct");

    {
        Application app(0, nullptr);
        File file(path);
        expect(file.open(File::OpenMode::ReadOnly), "WebAssembly File opens virtual FS path");
        const auto content = file.readAll();
        expect(content.has_value(), "WebAssembly File instance reads virtual FS data");
        expect(content.has_value() && content->size() == 4, "WebAssembly File instance read size is correct");
    }

    {
        Application app(0, nullptr);
        TempFile tempFile;
        expect(tempFile.open(), "WebAssembly TempFile opens under virtual /tmp");
        expect(! tempFile.filePath().empty(), "WebAssembly TempFile path is not empty");
        expect(File::exists(tempFile.filePath()), "WebAssembly TempFile exists in virtual FS");
        expect(tempFile.write(ByteArray(std::string("tmp"))), "WebAssembly TempFile writes data");
        expect(tempFile.flush(), "WebAssembly TempFile flushes data");
        const auto content = tempFile.readAll();
        expect(content.has_value() && content->size() == 3, "WebAssembly TempFile reads data");
    }

    expect(File::remove(path), "WebAssembly File removes virtual FS data");
}

void testWebSocketApiSurface()
{
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

    {
        Application app(0, nullptr);
        WebSocket socket;

        bool errorEmitted = false;
        std::string errorMessage;
        socket.errorOccurred.connect([&](const std::string& error) {
            errorEmitted = true;
            errorMessage = error;
        });

        socket.connectToHost(HostAddress("ws://localhost"), 8765, "/");

        expect(errorEmitted, "Emscripten WebSocket rejects invalid host strings");
        expect(socket.state() == WebSocketState::Error, "Invalid host WebSocket enters Error state");
        expect(errorMessage.find("Invalid") != std::string::npos, "Invalid host WebSocket error is descriptive");
    }

    {
        expect(WebSocket::currentOriginAddress().isEmpty(),
               "Current origin host is empty outside a browser page");
        expect(WebSocket::currentOriginPort() == 0, "Current origin port is zero outside a browser page");

        Application app(0, nullptr);
        WebSocket socket;

        bool errorEmitted = false;
        std::string errorMessage;
        socket.errorOccurred.connect([&](const std::string& error) {
            errorEmitted = true;
            errorMessage = error;
        });

        socket.connectToCurrentOrigin("/");

        expect(errorEmitted, "connectToCurrentOrigin reports missing browser location outside a page");
        expect(socket.state() == WebSocketState::Error, "Missing current origin enters Error state");
        expect(errorMessage.find("current browser location") != std::string::npos,
               "Missing current origin error is descriptive");
    }

    {
        Application app(0, nullptr);
        WebSocket socket;

        bool errorEmitted = false;
        socket.errorOccurred.connect([&](const std::string&) {
            errorEmitted = true;
        });

        socket.connectToCurrentHost(30123, "/");

        expect(errorEmitted, "connectToCurrentHost reports missing browser location outside a page");
        expect(socket.state() == WebSocketState::Error, "Missing current host enters Error state");
    }
}

}  // namespace

int main()
{
    testEventLoopPendingWork();
    testHostAddress();
    testFileApiSurface();
    testWebSocketApiSurface();

    if (failures != 0) {
        std::cerr << failures << " WebAssembly smoke assertion(s) failed\n";
        return 1;
    }

    std::cout << "WebAssembly smoke tests passed\n";
    return 0;
}
