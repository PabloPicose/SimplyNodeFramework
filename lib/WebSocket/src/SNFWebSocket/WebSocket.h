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
class WebSocketBackend;
}  // namespace websocket::detail

enum class WebSocketState : std::uint8_t {
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

    /**
     * @brief Returns the peer address for the current connection.
     *
     * Native builds query the underlying TCP socket when available. Emscripten
     * builds return the host passed to `connectToHost()`, because browsers do
     * not expose a socket-level peer address API.
     */
    HostAddress peerAddress() const;

    /**
     * @brief Returns the peer port, or 0 if no peer endpoint is known.
     */
    std::uint16_t peerPort() const;

    Signal<> connected;
    Signal<> disconnected;
    Signal<std::string> textMessageReceived;
    Signal<std::vector<std::uint8_t>> binaryMessageReceived;
    Signal<std::vector<std::uint8_t>> pongReceived;
    Signal<std::string> errorOccurred;

private:
    friend class WebSocketServer;

#ifndef __EMSCRIPTEN__
    explicit WebSocket(std::unique_ptr<TcpSocket> acceptedSocket, Node* parent = nullptr);
#endif
    void beginServerConnection();

private:
    std::unique_ptr<websocket::detail::WebSocketBackend> m_backend;
};

}  // namespace snf
