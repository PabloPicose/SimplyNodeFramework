#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/LocalServer.h>
#include <SNFNetwork/LocalSocket.h>

#include <sys/stat.h>

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
