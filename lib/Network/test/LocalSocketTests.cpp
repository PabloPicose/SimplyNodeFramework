#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/ByteArray.h>
#include <SNFNetwork/LocalServer.h>
#include <SNFNetwork/LocalSocket.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace snf;
using namespace std::chrono_literals;

namespace {

// ─── helpers ────────────────────────────────────────────────────────────────

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

// Unique per-test socket path to avoid cross-test collisions.
std::string makeTempPath(const std::string& tag)
{
    return "/tmp/snf_test_" + tag + "_" + std::to_string(::getpid()) + ".sock";
}

// ─── fixture using embedded LocalServer echo ────────────────────────────────

class LocalSocketFixture : public ::testing::Test
{
public:
    void SetUp() override
    {
        app = new Application(0, nullptr);

        m_serverPath = makeTempPath("echo");
        echoServer = new LocalServer();
        ASSERT_TRUE(echoServer->listen(m_serverPath)) << "Failed to start echo LocalServer";

        echoServer->newConnection.connect([this]() {
            LocalSocket* client = echoServer->nextPendingConnection();
            if (!client) {
                return;
            }
            acceptedSockets.push_back(client);

            client->readyRead.connect([client]() {
                const auto data = client->readAll();
                if (!data.empty()) {
                    client->write(data);  // echo back
                }
            });
        });
    }

    void TearDown() override
    {
        for (LocalSocket* s : acceptedSockets) {
            s->close();
            delete s;
        }
        acceptedSockets.clear();

        if (echoServer) {
            echoServer->close();
            delete echoServer;
            echoServer = nullptr;
        }

        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
    LocalServer* echoServer = nullptr;
    std::string m_serverPath;
    std::vector<LocalSocket*> acceptedSockets;
};

// ─── plain fixture (no embedded server) ─────────────────────────────────────

class LocalSocketPlainFixture : public ::testing::Test
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

}  // namespace

// ============================================================================
// LocalSocket — basic state tests (no server needed)
// ============================================================================

TEST_F(LocalSocketPlainFixture, stateIsDisconnectedInitially)
{
    LocalSocket socket;
    EXPECT_EQ(socket.state(), LocalSocketState::Disconnected);
}

TEST_F(LocalSocketPlainFixture, connectToNonExistentPathEmitsError)
{
    LocalSocket socket;

    std::string receivedError;
    socket.errorOccurred.connect([&](const std::string& err) { receivedError = err; });

    socket.connectToPath("/tmp/snf_no_such_socket_XXXXXX.sock");

    // Error state must be set synchronously (path doesn't exist).
    EXPECT_EQ(socket.state(), LocalSocketState::Error);
    EXPECT_FALSE(receivedError.empty());
}

TEST_F(LocalSocketPlainFixture, connectToTooLongPathEmitsError)
{
    LocalSocket socket;

    bool gotError = false;
    socket.errorOccurred.connect([&](const std::string&) { gotError = true; });

    // 108 chars exceeds the 107-byte limit for sun_path on Linux.
    socket.connectToPath(std::string(108, 'x'));

    EXPECT_TRUE(gotError);
    EXPECT_EQ(socket.state(), LocalSocketState::Error);
}

TEST_F(LocalSocketPlainFixture, writeEmptyPayloadReturnsZero)
{
    LocalSocket socket;

    EXPECT_EQ(socket.write(std::string()), 0u);
    EXPECT_EQ(socket.write(std::vector<std::uint8_t>{}), 0u);
    EXPECT_EQ(socket.write(ByteArray()), 0u);
}

TEST_F(LocalSocketPlainFixture, adoptedInvalidFdStaysDisconnected)
{
    // Invalid adopted fd must be ignored safely.
    LocalSocket socket(-1, false);

    EXPECT_EQ(socket.state(), LocalSocketState::Disconnected);
}

// ============================================================================
// LocalSocket — against an embedded LocalServer (no external dependency)
// ============================================================================

class LocalSocketExternalFixture : public ::testing::Test
{
public:
    void SetUp() override
    {
        app = new Application(0, nullptr);

        m_serverPath = makeTempPath("external");
        server = new LocalServer();
        ASSERT_TRUE(server->listen(m_serverPath)) << "Failed to start embedded LocalServer";

        // Accept incoming connections and keep them alive for the duration of
        // the test so the client stays in Connected state after connectToPath().
        server->newConnection.connect([this]() {
            LocalSocket* peer = server->nextPendingConnection();
            if (peer) {
                acceptedSockets.push_back(peer);
            }
        });
    }

    void TearDown() override
    {
        for (LocalSocket* s : acceptedSockets) {
            s->close();
            delete s;
        }
        acceptedSockets.clear();

        if (server) {
            server->close();
            delete server;
            server = nullptr;
        }

        delete app;
        app = nullptr;
    }

    Application* app = nullptr;
    LocalServer* server = nullptr;
    std::string m_serverPath;
    std::vector<LocalSocket*> acceptedSockets;
};

TEST_F(LocalSocketExternalFixture, connectToPathSucceeds)
{
    LocalSocket socket;

    bool connectedSignal = false;
    socket.connected.connect([&] { connectedSignal = true; });

    socket.connectToPath(m_serverPath);

    // UNIX domain sockets connect synchronously.
    EXPECT_TRUE(connectedSignal);
    EXPECT_EQ(socket.state(), LocalSocketState::Connected);
}

TEST_F(LocalSocketExternalFixture, closeTransitionsToDisconnected)
{
    LocalSocket socket;

    bool connectedSignal = false;
    socket.connected.connect([&] { connectedSignal = true; });
    socket.connectToPath(m_serverPath);

    ASSERT_TRUE(connectedSignal);

    bool disconnectedSignal = false;
    socket.disconnected.connect([&] { disconnectedSignal = true; });
    socket.close();

    EXPECT_EQ(socket.state(), LocalSocketState::Disconnected);
    EXPECT_TRUE(disconnectedSignal);
}

TEST_F(LocalSocketExternalFixture, disconnectedSignalEmittedOnClose)
{
    LocalSocket socket;
    bool disconnected = false;
    socket.disconnected.connect([&] { disconnected = true; });

    bool connectedSignal = false;
    socket.connected.connect([&] { connectedSignal = true; });
    socket.connectToPath(m_serverPath);

    ASSERT_TRUE(connectedSignal);
    socket.close();
    EXPECT_TRUE(disconnected);
}

TEST_F(LocalSocketExternalFixture, setBlockingFromOtherThreadIsApplied)
{
    LocalSocket socket(false);

    bool connectedSignal = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        connectedSignal = true;

        std::thread worker([&]() { socket.setBlocking(true); });
        worker.join();

        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& err) {
        errorMessage = err;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.connectToPath(m_serverPath);

    Timer t;
    armShutdown(t, 2s);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(connectedSignal);
    EXPECT_TRUE(socket.isBlocking());
}

TEST_F(LocalSocketExternalFixture, closeFromOtherThreadEmitsDisconnected)
{
    LocalSocket socket(false);

    bool connectedSignal = false;
    bool disconnectedSignal = false;
    std::string errorMessage;

    socket.connected.connect([&]() {
        connectedSignal = true;

        std::thread worker([&]() { socket.close(); });
        worker.join();
    });

    socket.disconnected.connect([&]() {
        disconnectedSignal = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.errorOccurred.connect([&](const std::string& err) {
        errorMessage = err;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    socket.connectToPath(m_serverPath);

    Timer t;
    armShutdown(t, 2s);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(connectedSignal);
    EXPECT_TRUE(disconnectedSignal);
    EXPECT_EQ(socket.state(), LocalSocketState::Disconnected);
}

// ============================================================================
// LocalSocket + LocalServer — integrated echo tests
// ============================================================================

TEST_F(LocalSocketFixture, connectAndReceiveConnectedSignal)
{
    LocalSocket client;

    bool connectedSignal = false;
    client.connected.connect([&] { connectedSignal = true; });

    // UNIX domain sockets connect synchronously — signal fires before app->run().
    client.connectToPath(m_serverPath);

    EXPECT_TRUE(connectedSignal);
    EXPECT_EQ(client.state(), LocalSocketState::Connected);

    // Give the server one pass to accept (cleanup).
    Timer t;
    armShutdown(t, 100ms);
    app->run();
}

TEST_F(LocalSocketFixture, echoRoundtrip)
{
    LocalSocket client;
    const std::string payload = "hello";

    // Write as soon as we're connected — UNIX connect is synchronous, so this
    // runs before app->run(), putting data in the kernel buffer before the
    // server even accepts. The server will read and echo it once the event
    // loop processes the accept.
    client.connected.connect([&] { client.write(payload); });

    std::vector<std::uint8_t> received;
    client.readyRead.connect([&] {
        const auto chunk = client.readAll();
        received.insert(received.end(), chunk.begin(), chunk.end());
        if (received.size() >= payload.size()) {
            if (EventLoop* loop = client.ownerEventLoop()) {
                loop->stop();
            }
        }
    });

    client.connectToPath(m_serverPath);

    Timer t;
    armShutdown(t, 2s);
    app->run();

    ASSERT_EQ(received.size(), payload.size());
    EXPECT_EQ(std::string(received.begin(), received.end()), payload);
}

TEST_F(LocalSocketFixture, bytesWrittenSignalEmitted)
{
    LocalSocket client;

    std::size_t totalWritten = 0;
    client.bytesWritten.connect([&](std::size_t n) {
        totalWritten += n;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->stop();
        }
    });

    // Write from the connected handler so everything stays in a single run.
    client.connected.connect([&] { client.write(std::string("ping")); });

    client.connectToPath(m_serverPath);

    Timer t;
    armShutdown(t, 2s);
    app->run();

    EXPECT_GT(totalWritten, 0u);
}

TEST_F(LocalSocketFixture, multipleClientsConnectSequentially)
{
    constexpr int kCount = 3;
    std::atomic<int> echoesReceived{0};

    // Allocate all clients on the heap and connect them all before app->run().
    // Each writes immediately from its connected handler (UNIX connect is
    // synchronous). The single event loop run processes all accepts and echoes.
    std::vector<LocalSocket*> clients;
    clients.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
        auto* client = new LocalSocket();
        clients.push_back(client);

        client->connected.connect([client] { client->write(std::string("ping")); });

        client->readyRead.connect([client, &echoesReceived, kCount] {
            client->readAll();
            if (++echoesReceived == kCount) {
                if (EventLoop* loop = client->ownerEventLoop()) {
                    loop->stop();
                }
            }
        });

        client->connectToPath(m_serverPath);
    }

    Timer t;
    armShutdown(t, 5s);
    app->run();

    for (LocalSocket* c : clients) {
        delete c;
    }

    EXPECT_EQ(echoesReceived.load(), kCount);
}

TEST_F(LocalSocketFixture, writeFromOtherThreadIsMarshaled)
{
    LocalSocket client;
    const std::string payload = "threaded-ping";

    bool connectedSignal = false;
    std::string errorMessage;
    std::vector<std::uint8_t> received;

    client.connected.connect([&]() {
        connectedSignal = true;

        std::thread writer([&]() { client.write(payload); });
        writer.join();
    });

    client.readyRead.connect([&]() {
        const auto chunk = client.readAll();
        received.insert(received.end(), chunk.begin(), chunk.end());
        if (received.size() >= payload.size()) {
            if (EventLoop* loop = client.ownerEventLoop()) {
                loop->stop();
            }
        }
    });

    client.errorOccurred.connect([&](const std::string& err) {
        errorMessage = err;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    client.connectToPath(m_serverPath);

    Timer t;
    armShutdown(t, 2s);
    app->run();

    EXPECT_TRUE(errorMessage.empty()) << "Error: " << errorMessage;
    EXPECT_TRUE(connectedSignal);
    ASSERT_EQ(received.size(), payload.size());
    EXPECT_EQ(std::string(received.begin(), received.end()), payload);
}

TEST_F(LocalSocketPlainFixture, byteArrayWriteStartsAtCurrentOffset)
{
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);

    LocalSocket socket(descriptors[0], false);
    descriptors[0] = -1;

    ByteArray bytes(std::string("ABCDE"));
    bytes.advance(2);

    EXPECT_EQ(socket.write(std::move(bytes)), 3u);

    std::uint8_t buffer[16] = {};
    const ssize_t readBytes = ::recv(descriptors[1], buffer, sizeof(buffer), 0);
    ::close(descriptors[1]);

    ASSERT_EQ(readBytes, 3);
    EXPECT_EQ(std::string(buffer, buffer + readBytes), "CDE");
}

TEST_F(LocalSocketPlainFixture, queuedByteArraysFlushInFifoOrder)
{
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);

    int sendBufferSize = 4096;
    ASSERT_EQ(::setsockopt(descriptors[0], SOL_SOCKET, SO_SNDBUF, &sendBufferSize, sizeof(sendBufferSize)), 0);

    LocalSocket socket(descriptors[0], false);
    descriptors[0] = -1;

    const std::size_t firstSize = 4 * 1024 * 1024;
    const std::size_t secondSize = 128 * 1024;
    const std::size_t thirdSize = 64 * 1024;

    const std::vector<std::uint8_t> first(firstSize, static_cast<std::uint8_t>('A'));
    const std::vector<std::uint8_t> second(secondSize, static_cast<std::uint8_t>('B'));
    const std::vector<std::uint8_t> third(thirdSize, static_cast<std::uint8_t>('C'));

    std::size_t totalBytesWritten = 0;
    socket.bytesWritten.connect([&](std::size_t written) { totalBytesWritten += written; });

    EXPECT_EQ(socket.write(ByteArray(first)), first.size());
    EXPECT_EQ(socket.write(ByteArray(second)), second.size());
    EXPECT_EQ(socket.write(ByteArray(third)), third.size());

    std::vector<std::uint8_t> received;
    received.reserve(firstSize + secondSize + thirdSize);

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (received.size() < firstSize + secondSize + thirdSize && std::chrono::steady_clock::now() < deadline) {
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->runPendingWork();
        }

        std::uint8_t buffer[64 * 1024];
        while (true) {
            const ssize_t readBytes = ::recv(descriptors[1], buffer, sizeof(buffer), MSG_DONTWAIT);
            if (readBytes > 0) {
                received.insert(received.end(), buffer, buffer + readBytes);
                continue;
            }

            if (readBytes == 0) {
                ADD_FAILURE() << "Peer closed before all queued data was read";
                break;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            ADD_FAILURE() << "recv() failed with errno " << errno;
            break;
        }

        if (received.size() < firstSize + secondSize + thirdSize) {
            std::this_thread::sleep_for(1ms);
        }
    }

    ::close(descriptors[1]);

    ASSERT_EQ(received.size(), firstSize + secondSize + thirdSize);
    EXPECT_EQ(totalBytesWritten, firstSize + secondSize + thirdSize);
    EXPECT_TRUE(std::all_of(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(firstSize),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('A'); }));
    EXPECT_TRUE(std::all_of(received.begin() + static_cast<std::ptrdiff_t>(firstSize),
                            received.begin() + static_cast<std::ptrdiff_t>(firstSize + secondSize),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('B'); }));
    EXPECT_TRUE(std::all_of(received.begin() + static_cast<std::ptrdiff_t>(firstSize + secondSize), received.end(),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('C'); }));
}

TEST_F(LocalSocketPlainFixture, closeClearsPendingByteArrayQueue)
{
    int descriptors[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);

    int sendBufferSize = 4096;
    ASSERT_EQ(::setsockopt(descriptors[0], SOL_SOCKET, SO_SNDBUF, &sendBufferSize, sizeof(sendBufferSize)), 0);

    LocalSocket socket(descriptors[0], false);
    descriptors[0] = -1;

    std::size_t totalBytesWritten = 0;
    socket.bytesWritten.connect([&](std::size_t written) { totalBytesWritten += written; });

    const std::size_t firstSize = 4 * 1024 * 1024;
    const std::vector<std::uint8_t> first(firstSize, static_cast<std::uint8_t>('A'));
    const std::vector<std::uint8_t> second(128 * 1024, static_cast<std::uint8_t>('B'));

    EXPECT_EQ(socket.write(ByteArray(first)), first.size());
    EXPECT_EQ(socket.write(ByteArray(second)), second.size());

    socket.close();

    std::vector<std::uint8_t> received;
    std::uint8_t buffer[64 * 1024] = {};
    while (true) {
        const ssize_t readBytes = ::recv(descriptors[1], buffer, sizeof(buffer), MSG_DONTWAIT);
        if (readBytes > 0) {
            received.insert(received.end(), buffer, buffer + readBytes);
            continue;
        }
        if (readBytes == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        ADD_FAILURE() << "recv() failed with errno " << errno;
        break;
    }

    ::close(descriptors[1]);

    EXPECT_EQ(socket.state(), LocalSocketState::Disconnected);
    EXPECT_EQ(totalBytesWritten, received.size());
    EXPECT_LT(received.size(), first.size() + second.size());
    EXPECT_TRUE(std::all_of(received.begin(), received.end(),
                            [](std::uint8_t byte) { return byte == static_cast<std::uint8_t>('A'); }));
}

// ============================================================================
// LocalServer — server-specific tests
// ============================================================================

TEST_F(LocalSocketPlainFixture, serverPathMatchesListenPath)
{
    const std::string path = makeTempPath("pathcheck");
    LocalServer server;
    ASSERT_TRUE(server.listen(path));

    EXPECT_EQ(server.serverPath(), path);

    server.close();
}

TEST_F(LocalSocketPlainFixture, serverCloseRemovesSocketFile)
{
    const std::string path = makeTempPath("unlink");
    LocalServer server;
    ASSERT_TRUE(server.listen(path));

    struct stat st{};
    EXPECT_EQ(::stat(path.c_str(), &st), 0) << "Socket file must exist after listen()";

    server.close();

    EXPECT_NE(::stat(path.c_str(), &st), 0) << "Socket file must be removed after close()";
}

TEST_F(LocalSocketPlainFixture, listenOnTooLongPathFails)
{
    LocalServer server;
    bool gotError = false;
    server.errorOccurred.connect([&](const std::string&) { gotError = true; });

    EXPECT_FALSE(server.listen(std::string(108, 'x')));
    EXPECT_TRUE(gotError);
}

TEST_F(LocalSocketPlainFixture, isListeningReflectsState)
{
    const std::string path = makeTempPath("islistening");
    LocalServer server;

    EXPECT_FALSE(server.isListening());
    ASSERT_TRUE(server.listen(path));
    EXPECT_TRUE(server.isListening());
    server.close();
    EXPECT_FALSE(server.isListening());
}

TEST_F(LocalSocketPlainFixture, disconnectSignalOnServerClose)
{
    const std::string path = makeTempPath("srvclose");
    LocalServer server;
    ASSERT_TRUE(server.listen(path));

    LocalSocket* accepted = nullptr;
    // Close the server-side socket from within the newConnection handler so
    // everything happens inside a single app->run() call.
    server.newConnection.connect([&] {
        accepted = server.nextPendingConnection();
        if (accepted) {
            accepted->close();
        }
    });

    LocalSocket client;
    bool disconnected = false;
    client.disconnected.connect([&] {
        disconnected = true;
        if (EventLoop* loop = client.ownerEventLoop()) {
            loop->stop();
        }
    });

    client.connectToPath(path);

    Timer t;
    armShutdown(t, 2s);
    app->run();

    EXPECT_TRUE(disconnected);

    delete accepted;
    server.close();
}
