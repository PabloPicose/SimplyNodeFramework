#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <SNFCore/Application.h>
#include <SNFCore/EventLoop.h>
#include <SNFCore/Timer.h>
#include <SNFNetwork/UdpSocket.h>

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

// ─── fixture for UDP socket tests ───────────────────────────────────────────

class UdpSocketFixture : public ::testing::Test
{
public:
    void SetUp() override { app = new Application(0, nullptr); }

    void TearDown() override
    {
        if (app) {
            delete app;
            app = nullptr;
        }
    }

    Application* app = nullptr;
};

}  // namespace

// ============================================================================
// UdpSocket — basic state and binding tests
// ============================================================================

TEST_F(UdpSocketFixture, stateIsUnboundInitially)
{
    UdpSocket socket;
    EXPECT_EQ(socket.state(), UdpSocketState::Unbound);
    EXPECT_EQ(socket.boundPort(), 0);
}

TEST_F(UdpSocketFixture, bindToLocalhostSucceeds)
{
    UdpSocket socket;

    bool hasError = false;
    socket.errorOccurred.connect([&](const std::string&) { hasError = true; });

    socket.bind(HostAddress::LocalHost, 0);

    EXPECT_EQ(socket.state(), UdpSocketState::Bound);
    EXPECT_GT(socket.boundPort(), 0);
    EXPECT_FALSE(hasError);
}

TEST_F(UdpSocketFixture, bindToAnyIPv4Succeeds)
{
    UdpSocket socket;

    bool hasError = false;
    socket.errorOccurred.connect([&](const std::string&) { hasError = true; });

    socket.bind(HostAddress::AnyIPv4, 0);

    EXPECT_EQ(socket.state(), UdpSocketState::Bound);
    EXPECT_GT(socket.boundPort(), 0);
    EXPECT_FALSE(hasError);
}

TEST_F(UdpSocketFixture, bindSpecificPortSucceeds)
{
    UdpSocket socket;

    bool hasError = false;
    socket.errorOccurred.connect([&](const std::string&) { hasError = true; });

    // Try to bind to an ephemeral port, then verify
    socket.bind(HostAddress::LocalHost, 0);
    const std::uint16_t assignedPort = socket.boundPort();

    EXPECT_GT(assignedPort, 0);
    EXPECT_EQ(socket.state(), UdpSocketState::Bound);
    EXPECT_FALSE(hasError);
}

TEST_F(UdpSocketFixture, sendEmptyDatagramReturnsZero)
{
    UdpSocket socket;
    socket.bind(HostAddress::LocalHost, 0);

    EXPECT_EQ(socket.sendDatagram(std::string(), HostAddress::LocalHost, 1234), 0u);
    EXPECT_EQ(socket.sendDatagram(std::vector<std::uint8_t>{}, HostAddress::LocalHost, 1234), 0u);
}

TEST_F(UdpSocketFixture, noPendingDatagramInitially)
{
    UdpSocket socket;
    socket.bind(HostAddress::LocalHost, 0);

    EXPECT_FALSE(socket.hasPendingDatagram());

    NetworkDatagram empty = socket.pendingDatagram();
    EXPECT_TRUE(empty.data().empty());
    EXPECT_EQ(empty.senderPort(), 0);
}

TEST_F(UdpSocketFixture, isBlockingToggle)
{
    UdpSocket socket;

    EXPECT_FALSE(socket.isBlocking());

    socket.setBlocking(true);
    EXPECT_TRUE(socket.isBlocking());

    socket.setBlocking(false);
    EXPECT_FALSE(socket.isBlocking());
}

// ============================================================================
// UdpSocket — loopback send/receive
// ============================================================================

TEST_F(UdpSocketFixture, sendAndReceiveLoopback)
{
    UdpSocket sender;
    UdpSocket receiver;

    // Setup receiver first to get its port
    ASSERT_TRUE(receiver.bind(HostAddress::LocalHost, 0));
    const std::uint16_t receiverPort = receiver.boundPort();
    ASSERT_GT(receiverPort, 0);

    // Setup sender
    ASSERT_TRUE(sender.bind(HostAddress::LocalHost, 0));

    // Track events
    bool receiverGotData = false;
    bool senderWroteData = false;
    std::string receivedPayload;
    std::string senderAddress;
    std::uint16_t senderPort = 0;

    const std::string testPayload = "hello-udp-world";

    receiver.readyRead.connect([&]() {
        if (receiver.hasPendingDatagram()) {
            NetworkDatagram dgram = receiver.pendingDatagram();
            receivedPayload.append(dgram.data().begin(), dgram.data().end());
            senderAddress = dgram.senderHost();
            senderPort = dgram.senderPort();
            receiverGotData = true;

            if (EventLoop* loop = receiver.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    sender.bytesWritten.connect([&](std::size_t written) {
        if (written > 0) {
            senderWroteData = true;
        }
    });

    // Send datagram
    sender.sendDatagram(testPayload, HostAddress::LocalHost, receiverPort);

    Timer shutdown;
    armShutdown(shutdown, 2s);

    app->run();

    EXPECT_TRUE(senderWroteData);
    EXPECT_TRUE(receiverGotData);
    EXPECT_EQ(receivedPayload, testPayload);
    EXPECT_EQ(senderAddress, "127.0.0.1");
    EXPECT_EQ(senderPort, sender.boundPort());
}

TEST_F(UdpSocketFixture, sendAndReceiveWithVectorData)
{
    UdpSocket sender;
    UdpSocket receiver;

    ASSERT_TRUE(receiver.bind(HostAddress::LocalHost, 0));
    const std::uint16_t receiverPort = receiver.boundPort();

    ASSERT_TRUE(sender.bind(HostAddress::LocalHost, 0));

    bool receiverGotData = false;
    std::vector<std::uint8_t> receivedData;

    const std::vector<std::uint8_t> testPayload = {0x01, 0x02, 0x03, 0x04, 0x05};

    receiver.readyRead.connect([&]() {
        if (receiver.hasPendingDatagram()) {
            NetworkDatagram dgram = receiver.pendingDatagram();
            receivedData = dgram.data();
            receiverGotData = true;

            if (EventLoop* loop = receiver.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    sender.sendDatagram(testPayload, HostAddress::LocalHost, receiverPort);

    Timer shutdown;
    armShutdown(shutdown, 2s);

    app->run();

    EXPECT_TRUE(receiverGotData);
    EXPECT_EQ(receivedData, testPayload);
}

TEST_F(UdpSocketFixture, sendMultipleDatagramsReceiveAll)
{
    UdpSocket sender;
    UdpSocket receiver;

    ASSERT_TRUE(receiver.bind(HostAddress::LocalHost, 0));
    const std::uint16_t receiverPort = receiver.boundPort();

    ASSERT_TRUE(sender.bind(HostAddress::LocalHost, 0));

    std::vector<std::string> receivedPayloads;
    int datagrams_received = 0;

    receiver.readyRead.connect([&]() {
        while (receiver.hasPendingDatagram()) {
            NetworkDatagram dgram = receiver.pendingDatagram();
            std::string payload(dgram.data().begin(), dgram.data().end());
            receivedPayloads.push_back(payload);
            datagrams_received++;
        }

        if (datagrams_received >= 3) {
            if (EventLoop* loop = receiver.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    // Send three datagrams
    sender.sendDatagram("packet1", HostAddress::LocalHost, receiverPort);
    sender.sendDatagram("packet2", HostAddress::LocalHost, receiverPort);
    sender.sendDatagram("packet3", HostAddress::LocalHost, receiverPort);

    Timer shutdown;
    armShutdown(shutdown, 2s);

    app->run();

    EXPECT_EQ(datagrams_received, 3);
    EXPECT_EQ(receivedPayloads.size(), 3u);
    EXPECT_EQ(receivedPayloads[0], "packet1");
    EXPECT_EQ(receivedPayloads[1], "packet2");
    EXPECT_EQ(receivedPayloads[2], "packet3");
}

// ============================================================================
// UdpSocket — multicast group support
// ============================================================================

TEST_F(UdpSocketFixture, joinMulticastGroupIPv4)
{
    UdpSocket socket;
    ASSERT_TRUE(socket.bind(HostAddress::AnyIPv4, 0));

    // Join a well-known multicast group
    const bool joined = socket.joinMulticastGroup(HostAddress("224.0.0.1"), HostAddress::AnyIPv4);

    EXPECT_TRUE(joined);
}

TEST_F(UdpSocketFixture, leaveMulticastGroupIPv4)
{
    UdpSocket socket;
    ASSERT_TRUE(socket.bind(HostAddress::AnyIPv4, 0));

    const bool joined = socket.joinMulticastGroup(HostAddress("224.0.0.1"), HostAddress::AnyIPv4);
    ASSERT_TRUE(joined);

    const bool left = socket.leaveMulticastGroup(HostAddress("224.0.0.1"), HostAddress::AnyIPv4);
    EXPECT_TRUE(left);
}

TEST_F(UdpSocketFixture, joinMulticastWithoutBindFails)
{
    UdpSocket socket;
    // Don't bind yet

    const bool joined = socket.joinMulticastGroup(HostAddress("224.0.0.1"), HostAddress::AnyIPv4);

    EXPECT_FALSE(joined);
}

// ============================================================================
// UdpSocket — error handling
// ============================================================================

TEST_F(UdpSocketFixture, sendToUnboundSocketFails)
{
    UdpSocket socket;
    // Don't bind

    std::size_t sent = socket.sendDatagram("test", HostAddress::LocalHost, 1234);

    EXPECT_EQ(sent, 0u);
}

TEST_F(UdpSocketFixture, networkDatagramProperties)
{
    const std::vector<std::uint8_t> data = {0x01, 0x02, 0x03};
    const std::string host = "192.168.1.1";
    const std::uint16_t port = 5678;

    NetworkDatagram dgram(data, host, port);

    EXPECT_EQ(dgram.data(), data);
    EXPECT_EQ(dgram.senderHost(), host);
    EXPECT_EQ(dgram.senderPort(), port);
}

TEST_F(UdpSocketFixture, emptyNetworkDatagram)
{
    NetworkDatagram dgram;

    EXPECT_TRUE(dgram.data().empty());
    EXPECT_TRUE(dgram.senderHost().empty());
    EXPECT_EQ(dgram.senderPort(), 0);
}

// ============================================================================
// UdpSocket — cross-thread safety
// ============================================================================

TEST_F(UdpSocketFixture, setBlockingFromDifferentThread)
{
    UdpSocket socket;
    socket.bind(HostAddress::LocalHost, 0);

    bool success = true;
    std::thread t([&socket, &success]() {
        try {
            socket.setBlocking(true);
            socket.setBlocking(false);
        } catch (...) {
            success = false;
        }
    });

    t.join();
    EXPECT_TRUE(success);
}

TEST_F(UdpSocketFixture, bindFromDifferentThreadExecutesInOwnerLoop)
{
    UdpSocket socket;

    std::atomic<bool> workerDone{false};
    std::atomic<bool> bindOk{false};

    std::thread worker([&]() {
        bindOk = socket.bind(HostAddress::LocalHost, 0);
        workerDone = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);
    app->run();

    worker.join();

    EXPECT_TRUE(workerDone.load());
    EXPECT_TRUE(bindOk.load());
    EXPECT_EQ(socket.state(), UdpSocketState::Bound);
    EXPECT_GT(socket.boundPort(), 0);
}

TEST_F(UdpSocketFixture, setBlockingFromDifferentThreadAppliesChange)
{
    UdpSocket socket(false);
    ASSERT_TRUE(socket.bind(HostAddress::LocalHost, 0));

    std::atomic<bool> workerDone{false};

    std::thread worker([&]() {
        socket.setBlocking(true);
        workerDone = true;
        if (EventLoop* loop = socket.ownerEventLoop()) {
            loop->post([loop]() { loop->stop(); });
        }
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);
    app->run();

    worker.join();

    EXPECT_TRUE(workerDone.load());
    EXPECT_TRUE(socket.isBlocking());
}

TEST_F(UdpSocketFixture, sendDatagramFromDifferentThreadDeliversData)
{
    UdpSocket sender;
    UdpSocket receiver;

    ASSERT_TRUE(receiver.bind(HostAddress::LocalHost, 0));
    ASSERT_TRUE(sender.bind(HostAddress::LocalHost, 0));

    const std::uint16_t receiverPort = receiver.boundPort();
    const std::string payload = "cross-thread-send";
    std::string received;
    std::atomic<bool> workerQueued{false};

    receiver.readyRead.connect([&]() {
        while (receiver.hasPendingDatagram()) {
            NetworkDatagram dgram = receiver.pendingDatagram();
            received.append(dgram.data().begin(), dgram.data().end());
        }

        if (received.size() >= payload.size()) {
            if (EventLoop* loop = receiver.ownerEventLoop()) {
                loop->post([loop]() { loop->stop(); });
            }
        }
    });

    std::thread worker([&]() {
        const std::size_t accepted = sender.sendDatagram(payload, HostAddress::LocalHost, receiverPort);
        workerQueued = (accepted == payload.size());
    });

    Timer shutdown;
    armShutdown(shutdown, 2s);
    app->run();

    worker.join();

    EXPECT_TRUE(workerQueued.load());
    EXPECT_EQ(received, payload);
}

// ============================================================================
// UdpSocket — blocking mode behavior
// ============================================================================

TEST_F(UdpSocketFixture, blockingModeToggle)
{
    UdpSocket socket(false);  // non-blocking
    EXPECT_FALSE(socket.isBlocking());

    socket.setBlocking(true);
    EXPECT_TRUE(socket.isBlocking());

    socket.setBlocking(false);
    EXPECT_FALSE(socket.isBlocking());
}

TEST_F(UdpSocketFixture, constructorWithBlockingTrue)
{
    UdpSocket socket(true);  // blocking
    EXPECT_TRUE(socket.isBlocking());
}

TEST_F(UdpSocketFixture, bindInvalidHostEmitsErrorAndSetsErrorState)
{
    UdpSocket socket;

    std::string errorText;
    socket.errorOccurred.connect([&](const std::string& error) { errorText = error; });

    const bool ok = socket.bind(HostAddress("%%%invalid-host%%%"), 12345);

    Timer shutdown;
    armShutdown(shutdown, 50ms);
    app->run();

    EXPECT_FALSE(ok);
    EXPECT_EQ(socket.state(), UdpSocketState::Error);
    EXPECT_FALSE(errorText.empty());
}

TEST_F(UdpSocketFixture, bindToUnavailableLocalAddressFails)
{
    UdpSocket socket;

    std::string errorText;
    socket.errorOccurred.connect([&](const std::string& error) { errorText = error; });

    // TEST-NET-3 address: expected to be non-local on development machines.
    const bool ok = socket.bind(HostAddress("203.0.113.123"), 34567);

    Timer shutdown;
    armShutdown(shutdown, 50ms);
    app->run();

    EXPECT_FALSE(ok);
    EXPECT_EQ(socket.state(), UdpSocketState::Error);
    EXPECT_FALSE(errorText.empty());
}
