#pragma once

/**
 * @file WebSocket.h
 * @brief Event-driven WebSocket connection for local ws:// communication.
 * @ingroup SNFWebSocket
 */

#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include "SNFNetwork/HostAddress.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snf {

class TcpSocket;

namespace websocket::detail {
enum class OpCode : std::uint8_t;
struct WebSocketFrame;
class WebSocketFrameParser;
}  // namespace websocket::detail

enum class WebSocketState {
    Closed,
    Connecting,
    Open,
    Closing,
    Error,
};

/**
 * @class WebSocket
 * @ingroup SNFWebSocket
 * @brief Non-blocking WebSocket endpoint for `ws://` client and server-side connections.
 *
 * A `WebSocket` can initiate an outgoing connection with `connectToHost()`.
 * Instances returned by `WebSocketServer::nextPendingConnection()` are already
 * open and ready to exchange messages.
 */
class WebSocket : public Node
{
public:
    explicit WebSocket(Node* parent = nullptr);
    ~WebSocket() override;

    void update() override {}

    /**
     * @brief Connects to a `ws://` endpoint and starts the HTTP Upgrade handshake.
     * @param address Remote host/address.
     * @param port Remote TCP port.
     * @param path WebSocket resource path, usually `/`.
     */
    void connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path = "/");

    /** @brief Sends a UTF-8 text message. Returns `false` if the socket is not open. */
    bool sendTextMessage(const std::string& message);

    /** @brief Sends a binary message. Returns `false` if the socket is not open. */
    bool sendBinaryMessage(const std::vector<std::uint8_t>& data);

    /** @brief Sends a ping control frame. */
    bool ping(const std::vector<std::uint8_t>& payload = {});

    /** @brief Starts a graceful WebSocket close. */
    void close();

    bool isOpen() const;
    bool isValid() const;
    WebSocketState state() const;

    Signal<> connected;
    Signal<> disconnected;
    Signal<std::string> textMessageReceived;
    Signal<std::vector<std::uint8_t>> binaryMessageReceived;
    Signal<std::vector<std::uint8_t>> pongReceived;
    Signal<std::string> errorOccurred;

private:
    friend class WebSocketServer;

    explicit WebSocket(std::unique_ptr<TcpSocket> acceptedSocket, Node* parent = nullptr);
    void beginServerConnection();

    void attachSocketSignals();
    void handleTcpConnected();
    void handleTcpReadyRead();
    void handleTcpDisconnected();
    void handleTcpError(const std::string& errorMessage);

    void processHandshakeBytes(const std::vector<std::uint8_t>& data);
    void processFrameBytes(const std::vector<std::uint8_t>& data);
    void processFrame(const websocket::detail::WebSocketFrame& frame);

    bool sendFrame(websocket::detail::OpCode opcode,
                   const std::vector<std::uint8_t>& payload,
                   bool requireOpen = true);

    void fail(std::string errorMessage);
    void setState(WebSocketState state);
    bool shouldMaskOutgoingFrames() const;

private:
    std::unique_ptr<TcpSocket> m_socket;
    WebSocketState m_state = WebSocketState::Closed;
    bool m_clientMode = true;
    bool m_handshakeComplete = false;
    bool m_closeFrameSent = false;
    std::string m_handshakeBuffer;
    std::string m_clientKey;
    std::string m_host;
    std::uint16_t m_port = 0;
    std::string m_path = "/";
    std::uint8_t m_fragmentOpcode = 0;
    std::vector<std::uint8_t> m_fragmentPayload;
    std::unique_ptr<websocket::detail::WebSocketFrameParser> m_frameParser;
};

}  // namespace snf
