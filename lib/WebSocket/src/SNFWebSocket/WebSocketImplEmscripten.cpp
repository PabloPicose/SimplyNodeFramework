#include "SNFWebSocket/WebSocketBackend.h"

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

#include <cstdlib>
#include <sstream>
#include <utility>

namespace snf {

namespace {

EM_JS(char*, snf_browser_origin_host, (), {
    if (typeof window === 'undefined' || !window.location) {
        return 0;
    }

    var host = window.location.hostname || "";
    if (host.length >= 2 && host[0] === '[' && host[host.length - 1] === ']') {
        host = host.substring(1, host.length - 1);
    }
    return stringToNewUTF8(host);
});

EM_JS(int, snf_browser_origin_port, (), {
    if (typeof window === 'undefined' || !window.location) {
        return 0;
    }

    var port = window.location.port || "";
    if (port) {
        return parseInt(port, 10) || 0;
    }

    if (window.location.protocol === 'https:') {
        return 443;
    }
    if (window.location.protocol === 'http:') {
        return 80;
    }
    return 0;
});

EM_JS(int, snf_browser_origin_is_secure, (), {
    if (typeof window === 'undefined' || !window.location) {
        return 0;
    }
    return window.location.protocol === 'https:' ? 1 : 0;
});

std::string takeOwnedCString(char* value)
{
    if (! value) {
        return {};
    }

    std::string output(value);
    std::free(value);
    return output;
}

std::string normalizedPath(const std::string& path)
{
    if (path.empty()) {
        return "/";
    }
    if (path.front() == '/') {
        return path;
    }
    return "/" + path;
}

std::string buildWebSocketUrl(const std::string& rawHost, std::uint16_t port, const std::string& path, bool secure)
{
    std::string host = rawHost;
    if (host.find(':') != std::string::npos && (host.empty() || host.front() != '[')) {
        host = "[" + host + "]";
    }

    std::ostringstream stream;
    stream << (secure ? "wss://" : "ws://") << host << ":" << port << normalizedPath(path);
    return stream.str();
}

std::string buildWebSocketUrl(const HostAddress& address, std::uint16_t port, const std::string& path)
{
    return buildWebSocketUrl(address.host(), port, path, false);
}

bool isWildcardConnectAddress(const HostAddress& address)
{
    return address.toString() == HostAddress::AnyIPv4.toString() ||
           address.toString() == HostAddress::AnyIPv6.toString();
}

}  // namespace

class WebSocketImplEmscripten final : public websocket::detail::WebSocketBackend
{
public:
    explicit WebSocketImplEmscripten(WebSocket& owner) : m_owner(owner) {}

    ~WebSocketImplEmscripten() override
    {
        if (m_socket > 0) {
            emscripten_websocket_close(m_socket, 1000, "");
            emscripten_websocket_delete(m_socket);
            m_socket = 0;
        }
    }

    void connectToHost(const HostAddress& address, std::uint16_t port, const std::string& path) override
    {
        close();

        if (! address.isValid()) {
            fail("Invalid WebSocket host address '" + address.toString() +
                 "'. Pass a hostname or IP address, not a URL.");
            return;
        }

        if (isWildcardConnectAddress(address)) {
            fail("WebSocket client cannot connect to wildcard address '" + address.toString() +
                 "'. Use a concrete peer address such as 127.0.0.1.");
            return;
        }

        if (!emscripten_websocket_is_supported()) {
            fail("Emscripten WebSocket API is not supported in this environment");
            return;
        }

        m_url = buildWebSocketUrl(address, port, path);
        m_peerAddress = address;
        m_peerPort = port;

        EmscriptenWebSocketCreateAttributes attributes;
        emscripten_websocket_init_create_attributes(&attributes);
        attributes.url = m_url.c_str();
        attributes.protocols = nullptr;
        attributes.createOnMainThread = true;

        m_socket = emscripten_websocket_new(&attributes);
        if (m_socket <= 0) {
            m_socket = 0;
            fail("Failed to create Emscripten WebSocket");
            return;
        }

        emscripten_websocket_set_onopen_callback(m_socket, this, &WebSocketImplEmscripten::onOpen);
        emscripten_websocket_set_onmessage_callback(m_socket, this, &WebSocketImplEmscripten::onMessage);
        emscripten_websocket_set_onerror_callback(m_socket, this, &WebSocketImplEmscripten::onError);
        emscripten_websocket_set_onclose_callback(m_socket, this, &WebSocketImplEmscripten::onClose);

        setState(WebSocketState::Connecting);
    }

#ifdef __EMSCRIPTEN__
    void connectToCurrentOrigin(const std::string& path) override
    {
        close();

        const HostAddress address = WebSocket::currentOriginAddress();
        const std::uint16_t port = WebSocket::currentOriginPort();
        const bool secure = WebSocket::currentOriginIsSecure();

        if (address.isEmpty() || port == 0) {
            fail("Cannot derive WebSocket URL from current browser location");
            return;
        }

        connectToBrowserUrl(address, port, path, secure);
    }

    void connectToCurrentHost(std::uint16_t port, const std::string& path) override
    {
        close();

        const HostAddress address = WebSocket::currentOriginAddress();
        const bool secure = WebSocket::currentOriginIsSecure();

        if (address.isEmpty() || port == 0) {
            fail("Cannot derive WebSocket URL from current browser location");
            return;
        }

        connectToBrowserUrl(address, port, path, secure);
    }
#endif

    bool sendTextMessage(const std::string& message) override
    {
        if (!isOpen() || m_socket <= 0) {
            return false;
        }
        return emscripten_websocket_send_utf8_text(m_socket, message.c_str()) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    bool sendBinaryMessage(const std::vector<std::uint8_t>& data) override
    {
        if (!isOpen() || m_socket <= 0) {
            return false;
        }

        std::uint8_t emptyByte = 0;
        void* bytes = data.empty() ? static_cast<void*>(&emptyByte)
                                   : static_cast<void*>(const_cast<std::uint8_t*>(data.data()));
        return emscripten_websocket_send_binary(m_socket, bytes, static_cast<std::uint32_t>(data.size())) ==
               EMSCRIPTEN_RESULT_SUCCESS;
    }

    bool ping(const std::vector<std::uint8_t>&) override
    {
        return false;
    }

    void close() override
    {
        if (m_socket <= 0) {
            setState(WebSocketState::Closed);
            return;
        }

        if (m_state == WebSocketState::Open || m_state == WebSocketState::Connecting) {
            setState(WebSocketState::Closing);
            emscripten_websocket_close(m_socket, 1000, "");
        }
    }

    bool isOpen() const override { return m_state == WebSocketState::Open; }
    bool isValid() const override { return m_state != WebSocketState::Error; }
    WebSocketState state() const override { return m_state; }
    HostAddress peerAddress() const override { return m_peerAddress; }
    std::uint16_t peerPort() const override { return m_peerPort; }

    void beginServerConnection() override
    {
        fail("WebSocketServer is not supported in Emscripten builds");
    }

private:
    void connectToBrowserUrl(const HostAddress& address, std::uint16_t port, const std::string& path, bool secure)
    {
        if (!emscripten_websocket_is_supported()) {
            fail("Emscripten WebSocket API is not supported in this environment");
            return;
        }

        m_url = buildWebSocketUrl(address.host(), port, path, secure);
        m_peerAddress = address;
        m_peerPort = port;

        EmscriptenWebSocketCreateAttributes attributes;
        emscripten_websocket_init_create_attributes(&attributes);
        attributes.url = m_url.c_str();
        attributes.protocols = nullptr;
        attributes.createOnMainThread = true;

        m_socket = emscripten_websocket_new(&attributes);
        if (m_socket <= 0) {
            m_socket = 0;
            fail("Failed to create Emscripten WebSocket");
            return;
        }

        emscripten_websocket_set_onopen_callback(m_socket, this, &WebSocketImplEmscripten::onOpen);
        emscripten_websocket_set_onmessage_callback(m_socket, this, &WebSocketImplEmscripten::onMessage);
        emscripten_websocket_set_onerror_callback(m_socket, this, &WebSocketImplEmscripten::onError);
        emscripten_websocket_set_onclose_callback(m_socket, this, &WebSocketImplEmscripten::onClose);

        setState(WebSocketState::Connecting);
    }

    static bool onOpen(int, const EmscriptenWebSocketOpenEvent*, void* userData)
    {
        static_cast<WebSocketImplEmscripten*>(userData)->handleOpen();
        return true;
    }

    static bool onMessage(int, const EmscriptenWebSocketMessageEvent* event, void* userData)
    {
        static_cast<WebSocketImplEmscripten*>(userData)->handleMessage(*event);
        return true;
    }

    static bool onError(int, const EmscriptenWebSocketErrorEvent*, void* userData)
    {
        static_cast<WebSocketImplEmscripten*>(userData)->fail("Emscripten WebSocket error");
        return true;
    }

    static bool onClose(int, const EmscriptenWebSocketCloseEvent* event, void* userData)
    {
        static_cast<WebSocketImplEmscripten*>(userData)->handleClose(*event);
        return true;
    }

    void handleOpen()
    {
        setState(WebSocketState::Open);
        m_owner.connected.emit();
    }

    void handleMessage(const EmscriptenWebSocketMessageEvent& event)
    {
        if (event.isText) {
            std::string message;
            if (event.numBytes > 0 && event.data) {
                message.assign(reinterpret_cast<const char*>(event.data), event.numBytes);
            }
            m_owner.textMessageReceived.emit(message);
            return;
        }

        std::vector<std::uint8_t> message;
        if (event.numBytes > 0 && event.data) {
            message.assign(event.data, event.data + event.numBytes);
        }
        m_owner.binaryMessageReceived.emit(message);
    }

    void handleClose(const EmscriptenWebSocketCloseEvent&)
    {
        const bool wasActive = m_state == WebSocketState::Connecting ||
                               m_state == WebSocketState::Open ||
                               m_state == WebSocketState::Closing;
        setState(WebSocketState::Closed);
        if (m_socket > 0) {
            emscripten_websocket_delete(m_socket);
            m_socket = 0;
        }
        if (wasActive) {
            m_owner.disconnected.emit();
        }
    }

    void fail(std::string errorMessage)
    {
        setState(WebSocketState::Error);
        m_owner.errorOccurred.emit(std::move(errorMessage));
    }

    void setState(WebSocketState state) { m_state = state; }

private:
    WebSocket& m_owner;
    EMSCRIPTEN_WEBSOCKET_T m_socket = 0;
    WebSocketState m_state = WebSocketState::Closed;
    std::string m_url;
    HostAddress m_peerAddress;
    std::uint16_t m_peerPort = 0;
};

namespace websocket::detail {

std::unique_ptr<WebSocketBackend> createWebSocketBackend(WebSocket& owner)
{
    return std::make_unique<WebSocketImplEmscripten>(owner);
}

}  // namespace websocket::detail

HostAddress WebSocket::currentOriginAddress()
{
    return HostAddress(takeOwnedCString(snf_browser_origin_host()));
}

std::uint16_t WebSocket::currentOriginPort()
{
    const int port = snf_browser_origin_port();
    if (port <= 0 || port > 65535) {
        return 0;
    }
    return static_cast<std::uint16_t>(port);
}

bool WebSocket::currentOriginIsSecure()
{
    return snf_browser_origin_is_secure() != 0;
}

}  // namespace snf
