#pragma once

#include "SNFNetwork/HostAddress.h"
#include "SNFWebSocket/WebSocket.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snf {

class TcpSocket;

namespace websocket::detail {

class WebSocketBackend
{
public:
    virtual ~WebSocketBackend() = default;

    virtual void connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path) = 0;
    virtual bool sendTextMessage(const std::string& message) = 0;
    virtual bool sendBinaryMessage(const std::vector<std::uint8_t>& data) = 0;
    virtual bool ping(const std::vector<std::uint8_t>& payload) = 0;
    virtual void close() = 0;

    virtual bool isOpen() const = 0;
    virtual bool isValid() const = 0;
    virtual WebSocketState state() const = 0;
    virtual HostAddress peerAddress() const = 0;
    virtual std::uint16_t peerPort() const = 0;

#ifdef __EMSCRIPTEN__
    virtual void connectToCurrentOrigin(const std::string& path) = 0;
    virtual void connectToCurrentHost(std::uint16_t port, const std::string& path) = 0;
#endif

    virtual void beginServerConnection() = 0;
};

std::unique_ptr<WebSocketBackend> createWebSocketBackend(WebSocket& owner);

#ifndef __EMSCRIPTEN__
std::unique_ptr<WebSocketBackend> createWebSocketNativeBackend(WebSocket& owner,
                                                               std::unique_ptr<TcpSocket> acceptedSocket);
#endif

}  // namespace websocket::detail

}  // namespace snf
