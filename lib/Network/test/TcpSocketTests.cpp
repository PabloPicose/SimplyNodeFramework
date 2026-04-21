#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/TcpSocket.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

class TcpSocketFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }

    void TearDown() override
    {
        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
};

void armShutdown(Timer& timer, std::chrono::milliseconds timeout)
{
    timer.setSingleShot(true);
    timer.timeout.connect([&timer]() {
        if (EventLoop* loop = timer.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });
    timer.start(timeout);
}

}  // namespace

TEST_F(TcpSocketFixture, connectAndEchoRoundTrip)
{
    TcpSocket socket(false);

    const std::string payload = "snf-echo-payload";

    bool didConnect = false;
    bool didReadyRead = false;
    bool didWrite = false;
    std::string received;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        socket.write(payload);
    });

    socket.bytesWritten.connect([&](std::size_t written) {
        if (written > 0) {
            didWrite = true;
        }
    });

    socket.readyRead.connect([&]() {
        didReadyRead = true;
        const std::vector<std::uint8_t> data = socket.readAll();
        received.append(data.begin(), data.end());
        if (received.size() >= payload.size()) {
            if (EventLoop* loop = socket.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    socket.connectToHost("127.0.0.1", 12345);
    app->run();

    if (! didConnect && ! errorMessage.empty()) {
        GTEST_SKIP() << "Echo server unavailable at 127.0.0.1:12345: " << errorMessage;
    }

    EXPECT_TRUE(errorMessage.empty());
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didWrite);
    EXPECT_TRUE(didReadyRead);
    EXPECT_EQ(received, payload);
}

TEST_F(TcpSocketFixture, closeEmitsDisconnected)
{
    TcpSocket socket(false);

    bool didConnect = false;
    bool didDisconnect = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        didConnect = true;
        socket.close();
    });

    socket.disconnected.connect([&]() {
        didDisconnect = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& error) {
        errorMessage = error;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);

    socket.connectToHost("127.0.0.1", 12345);
    app->run();

    if (! didConnect && ! errorMessage.empty()) {
        GTEST_SKIP() << "Echo server unavailable at 127.0.0.1:12345: " << errorMessage;
    }

    EXPECT_TRUE(errorMessage.empty());
    EXPECT_TRUE(didConnect);
    EXPECT_TRUE(didDisconnect);
    EXPECT_EQ(socket.state(), TcpSocketState::Disconnected);
}

TEST_F(TcpSocketFixture, defaultIsNonBlockingAndCanChangeMode)
{
    TcpSocket socket;

    EXPECT_FALSE(socket.isBlocking());
    socket.setBlocking(true);
    EXPECT_TRUE(socket.isBlocking());
    socket.setBlocking(false);
    EXPECT_FALSE(socket.isBlocking());
}
