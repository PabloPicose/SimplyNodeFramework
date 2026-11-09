#pragma once

/**
 * @file HttpServer.h
 * @brief Lightweight HTTP/1.1 server for development and local testing.
 * @ingroup SNFHttpServer
 */

#include "SNFCore/Connection.h"
#include "SNFCore/Node.h"
#include "SNFHttpServer/HttpRequest.h"
#include "SNFHttpServer/HttpResponse.h"
#include "SNFHttpServer/HttpRequestParser.h"
#include "SNFNetwork/HostAddress.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace snf {

class TcpServer;
class TcpSocket;

/**
 * @class HttpServer
 * @ingroup SNFHttpServer
 * @brief A minimal HTTP/1.1 server for development and testing.
 *
 * Features:
 * - GET and POST method routing
 * - Exact path matching (no wildcards, no regex)
 * - Query string parsing
 * - Static file serving with automatic MIME type detection
 * - Customizable error responses
 * - Signal-based callbacks (on_started, on_error)
 * - Thread-safe: each connection served on the owner's EventLoop thread
 *
 * - Protocol upgrade hand-off (e.g. WebSocket on the same port) via upgrade()
 *
 * Note: HTTP/1.1 without persistence; each request closes the connection.
 *       Not suitable for production use; designed for local development only.
 *
 * @code
 * HttpServer server;
 * 
 * server.on_started.connect([](uint16_t port) {
 *     std::cout << "Server started on port " << port << "\n";
 * });
 *
 * server.on_error.connect([](const std::string& error) {
 *     std::cerr << error << "\n";
 * });
 *
 * server.get("/api/status", [](const HttpRequest&) {
 *     HttpResponse resp;
 *     resp.json(R"({"ok":true})");
 *     return resp;
 * });
 *
 * server.post("/api/command", [](const HttpRequest& req) {
 *     std::cout << req.body << "\n";
 *     HttpResponse resp;
 *     resp.text("received");
 *     return resp;
 * });
 *
 * server.serve_static("/", "./www");
 * server.listen(HostAddress::LocalHost, 8080);
 * @endcode
 */
class HttpServer : public Node
{
public:
    /** @brief Request handler function type. */
    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

    /**
     * @brief Upgrade handler function type.
     *
     * Receives the connection socket and every byte read from it so far: the
     * complete Upgrade request followed by any bytes the client sent after it.
     * The handler takes ownership of the socket (it must eventually close and
     * delete it, e.g. with `deleteLater()`).
     */
    using UpgradeHandler = std::function<void(TcpSocket* socket, std::string alreadyRead)>;

    /** @brief Constructs an HTTP server, optionally attaching to @p parent. */
    explicit HttpServer(Node* parent = nullptr);
    ~HttpServer() override;

    /** @brief Node update method (required by Node interface). */
    void update() override {}

    /**
     * @brief Registers a GET route handler.
     * @param path The exact path to match (e.g., "/api/status").
     * @param handler Callback invoked when a GET request matches this path.
     */
    void get(const std::string& path, const RequestHandler& handler);

    /**
     * @brief Registers a POST route handler.
     * @param path The exact path to match (e.g., "/api/command").
     * @param handler Callback invoked when a POST request matches this path.
     */
    void post(const std::string& path, const RequestHandler& handler);

    /**
     * @brief Registers a static file serving prefix.
     * @param urlPrefix URL path prefix (e.g., "/" or "/static").
     * @param fsPath Filesystem directory path (e.g., "./www").
     *
     * When a request path starts with @p urlPrefix and no dynamic route matches,
     * the server attempts to serve the corresponding file from @p fsPath.
     * MIME types are inferred from file extensions.
     *
     * Multiple prefixes can be registered; they are checked in reverse registration order.
     */
    void serve_static(const std::string& urlPrefix, const std::string& fsPath);

    /**
     * @brief Registers a protocol upgrade route (e.g. WebSocket on the same port).
     * @param path The exact path to match (e.g., "/ws").
     * @param handler Callback that receives the connection.
     *
     * When a GET request to @p path carries `Upgrade: websocket`, the server
     * does not answer it: it stops reading the socket, forgets it and passes
     * it to @p handler together with the bytes already read (see
     * UpgradeHandler). From then on the socket belongs to the handler;
     * close() does not affect it.
     *
     * Error cases answered by the server itself (then the connection closes):
     * - A request to @p path without `Upgrade: websocket`: 426 Upgrade Required.
     * - `Upgrade: websocket` to a path not registered here: 404 Not Found.
     *
     * The handler runs on the server's EventLoop thread; whatever adopts the
     * socket (e.g. `WebSocketServer::handleUpgrade`) must live on that thread.
     *
     * @code
     * HttpServer http;
     * WebSocketServer ws;
     * http.upgrade("/ws", [&](TcpSocket* s, std::string data) {
     *     ws.handleUpgrade(s, std::move(data));
     * });
     * http.listen(HostAddress::LocalHost, 8080);
     * @endcode
     */
    void upgrade(const std::string& path, const UpgradeHandler& handler);

    /**
     * @brief Starts listening for incoming connections.
     * @param host Local address to bind to (e.g., HostAddress::LocalHost or HostAddress::AnyIPv4).
     * @param port Port number (0 to let the OS choose).
     * @return `true` on success, `false` on failure.
     *
     * On success, `on_started` is emitted with the actual listening port.
     * On failure, `on_error` is emitted and the server does not start.
     */
    bool listen(const HostAddress& host, std::uint16_t port);

    /**
     * @brief Stops listening for new connections.
     *
     * Sockets already handed to an upgrade handler are not affected.
     */
    void close();

    /**
     * @brief Returns the port the server is currently listening on, or 0 if not listening.
     */
    std::uint16_t serverPort() const;

    // Signals

    /** @brief Emitted when the server successfully starts listening. */
    Signal<std::uint16_t> on_started;

    /** @brief Emitted when a fatal server error occurs. */
    Signal<std::string> on_error;

private:
    struct RouteKey
    {
        std::string method;
        std::string path;

        bool operator<(const RouteKey& other) const
        {
            if (method != other.method)
                return method < other.method;
            return path < other.path;
        }
    };

    struct StaticPrefix
    {
        std::string urlPrefix;
        std::string fsPath;
    };

    struct SocketState
    {
        HttpRequestParser parser;
        std::string raw;                   ///< Bytes read so far (only kept when upgrade routes exist).
        std::vector<Connection> slots;     ///< Signal connections to drop on upgrade hand-off.
        bool        pendingClose  = false; ///< Close socket after write buffer drains.
        std::size_t totalToSend   = 0;     ///< Bytes expected to be sent for current response.
        std::size_t totalSent     = 0;     ///< Bytes confirmed sent via bytesWritten so far.
    };

    std::unique_ptr<TcpServer> _tcpServer;
    std::map<RouteKey, RequestHandler> _routes;
    std::vector<StaticPrefix> _staticPrefixes;
    std::map<std::string, UpgradeHandler> _upgradeRoutes;
    std::map<TcpSocket*, SocketState> _socketStates;

    // Private methods
    void _onNewConnection();
    void _onSocketReadyRead(TcpSocket* socket);
    void _onSocketError(TcpSocket* socket, const std::string& errorMsg);
    void _closeSocket(TcpSocket* socket);
    void _sendAndClose(TcpSocket* socket, const HttpResponse& response);
    void _handOff(TcpSocket* socket, const UpgradeHandler& handler);
    std::string _guessContentType(const std::string& filePath) const;
    bool _fileExists(const std::string& path) const;
    std::string _readFile(const std::string& path) const;
    std::string _resolvePath(const std::string& urlPrefix, const std::string& fsPath,
                              const std::string& requestPath) const;
};

} // namespace snf
