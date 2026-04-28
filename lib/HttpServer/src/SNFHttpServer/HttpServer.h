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

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

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
 * server.on_error.connect([](const Error& error) {
 *     std::cerr << error.message() << "\n";
 * });
 *
 * server.get("/api/status", [](const HttpRequest&) {
 *     return HttpResponse::json(R"({"ok":true})");
 * });
 *
 * server.post("/api/command", [](const HttpRequest& req) {
 *     std::cout << req.body() << "\n";
 *     return HttpResponse::text("received");
 * });
 *
 * server.serve_static("/", "./www");
 * server.listen("127.0.0.1", 8080);
 * @endcode
 */
class HttpServer : public Node
{
public:
    /** @brief Request handler function type. */
    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

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
     * @brief Starts listening for incoming connections.
     * @param host Local address to bind to (e.g., "127.0.0.1", "localhost", "0.0.0.0").
     * @param port Port number (0 to let the OS choose).
     * @return `true` on success, `false` on failure.
     *
     * On success, `on_started` is emitted with the actual listening port.
     * On failure, `on_error` is emitted and the server does not start.
     */
    bool listen(const std::string& host, std::uint16_t port);

    /**
     * @brief Stops the server and closes all connections.
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
    };

    std::unique_ptr<TcpServer> _tcpServer;
    std::map<RouteKey, RequestHandler> _routes;
    std::vector<StaticPrefix> _staticPrefixes;
    std::map<TcpSocket*, SocketState> _socketStates;

    // Private methods
    void _onNewConnection();
    void _onSocketReadyRead(TcpSocket* socket);
    void _onSocketError(TcpSocket* socket, const std::string& errorMsg);
    void _closeSocket(TcpSocket* socket);
    std::string _guessContentType(const std::string& filePath) const;
    bool _fileExists(const std::string& path) const;
    std::string _readFile(const std::string& path) const;
    std::string _resolvePath(const std::string& urlPrefix, const std::string& fsPath,
                              const std::string& requestPath) const;
};

} // namespace snf
