#include "SNFHttpServer/HttpServer.h"

#include "SNFNetwork/HostAddress.h"
#include "SNFNetwork/TcpServer.h"
#include "SNFNetwork/TcpSocket.h"
#include "SNFHttpServer/HttpRequestParser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace snf {

HttpServer::HttpServer(Node* parent)
    : Node()
{
    if (parent)
        parent->addChild(this);

    _tcpServer = std::make_unique<TcpServer>(this);
}

HttpServer::~HttpServer() = default;

void HttpServer::get(const std::string& path, const RequestHandler& handler)
{
    _routes[RouteKey{"GET", path}] = handler;
}

void HttpServer::post(const std::string& path, const RequestHandler& handler)
{
    _routes[RouteKey{"POST", path}] = handler;
}

void HttpServer::serve_static(const std::string& urlPrefix, const std::string& fsPath)
{
    _staticPrefixes.push_back({urlPrefix, fsPath});
}

bool HttpServer::listen(const std::string& host, std::uint16_t port)
{
    if (!_tcpServer)
    {
        on_error.emit("HttpServer internal error: TcpServer not initialized");
        return false;
    }

    // Connect signals
    _tcpServer->newConnection.connect([this]() { _onNewConnection(); });
    _tcpServer->errorOccurred.connect([this](const std::string& msg) {
        on_error.emit(msg);
    });

    // Start listening
    bool success = _tcpServer->listen(host, port);
    if (success)
    {
        on_started.emit(_tcpServer->serverPort());
    }
    else
    {
        on_error.emit("Failed to bind to " + host + ":" + std::to_string(port));
    }

    return success;
}

void HttpServer::close()
{
    if (_tcpServer)
    {
        _tcpServer->close();
    }
}

std::uint16_t HttpServer::serverPort() const
{
    if (_tcpServer)
        return _tcpServer->serverPort();
    return 0;
}

void HttpServer::_onNewConnection()
{
    while (_tcpServer->hasPendingConnections())
    {
        TcpSocket* socket = _tcpServer->nextPendingConnection();
        if (!socket)
            continue;

        // Initialize socket state (parser)
        _socketStates[socket] = SocketState();

        // Connect signals
        socket->readyRead.connect([this, socket]() { _onSocketReadyRead(socket); });
        socket->disconnected.connect([this, socket]() { _closeSocket(socket); });
        socket->errorOccurred.connect([this, socket](const std::string& msg) {
            _onSocketError(socket, msg);
        });
    }
}

void HttpServer::_onSocketReadyRead(TcpSocket* socket)
{
    if (!socket)
        return;

    // Read available data
    auto data = socket->readAll();
    if (data.empty())
        return;

    // Get parser for this socket
    auto stateIt = _socketStates.find(socket);
    if (stateIt == _socketStates.end())
        return;

    HttpRequestParser& parser = stateIt->second.parser;

    // Feed data to parser
    parser.feed(data.data(), data.size());

    // Process any complete requests
    while (parser.isComplete())
    {
        HttpRequest request = parser.parse();

        // Find matching route
        RouteKey key{request.method, request.path};
        auto routeIt = _routes.find(key);

        HttpResponse response;

        if (routeIt != _routes.end())
        {
            // Route found, call handler
            try
            {
                response = routeIt->second(request);
            }
            catch (const std::exception& e)
            {
                response = HttpResponse::internalServerError(e.what());
            }
        }
        else
        {
            // Try to match static files
            bool staticHandled = false;

            // Check static prefixes in reverse order (most recent first)
            for (auto it = _staticPrefixes.rbegin(); it != _staticPrefixes.rend(); ++it)
            {
                if (request.path.substr(0, it->urlPrefix.length()) == it->urlPrefix)
                {
                    std::string relativePath = request.path.substr(it->urlPrefix.length());
                    if (relativePath.empty())
                        relativePath = "index.html";

                    std::string resolvedPath = _resolvePath(it->urlPrefix, it->fsPath,
                                                              relativePath);

                    if (_fileExists(resolvedPath))
                    {
                        response.status(200);
                        response.headers["Content-Type"] = _guessContentType(resolvedPath);
                        response.body = _readFile(resolvedPath);
                        staticHandled = true;
                        break;
                    }
                }
            }

            if (!staticHandled)
            {
                response = HttpResponse::notFound();
            }
        }

        // Send response
        std::string serialized = response.serialize();
        socket->write(serialized);

        // Close connection (HTTP/1.1 without Keep-Alive)
        socket->close();
        _socketStates.erase(socket);
        return;
    }

    if (parser.hasError())
    {
        HttpResponse errorResponse = HttpResponse::badRequest(parser.error());
        std::string serialized = errorResponse.serialize();
        socket->write(serialized);
        socket->close();
        _socketStates.erase(socket);
    }
}

void HttpServer::_onSocketError(TcpSocket* socket, const std::string& errorMsg)
{
    on_error.emit(errorMsg);
    _closeSocket(socket);
}

void HttpServer::_closeSocket(TcpSocket* socket)
{
    if (socket)
    {
        socket->close();
        _socketStates.erase(socket);
        socket->deleteLater();
    }
}

std::string HttpServer::_guessContentType(const std::string& filePath) const
{
    // Extract extension
    std::size_t dotPos = filePath.rfind('.');
    if (dotPos == std::string::npos)
        return "application/octet-stream";

    std::string ext = filePath.substr(dotPos);

    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Common MIME types (same as in HttpResponse)
    if (ext == ".html" || ext == ".htm")
        return "text/html";
    if (ext == ".css")
        return "text/css";
    if (ext == ".js")
        return "application/javascript";
    if (ext == ".json")
        return "application/json";
    if (ext == ".xml")
        return "application/xml";
    if (ext == ".txt")
        return "text/plain";
    if (ext == ".png")
        return "image/png";
    if (ext == ".jpg" || ext == ".jpeg")
        return "image/jpeg";
    if (ext == ".gif")
        return "image/gif";
    if (ext == ".svg")
        return "image/svg+xml";
    if (ext == ".ico")
        return "image/x-icon";
    if (ext == ".webp")
        return "image/webp";
    if (ext == ".wasm")
        return "application/wasm";
    if (ext == ".woff")
        return "font/woff";
    if (ext == ".woff2")
        return "font/woff2";
    if (ext == ".ttf")
        return "font/ttf";
    if (ext == ".pdf")
        return "application/pdf";
    if (ext == ".zip")
        return "application/zip";
    if (ext == ".mp4")
        return "video/mp4";
    if (ext == ".mp3")
        return "audio/mpeg";
    if (ext == ".wav")
        return "audio/wav";

    return "application/octet-stream";
}

bool HttpServer::_fileExists(const std::string& path) const
{
    try
    {
        return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
    }
    catch (...)
    {
        return false;
    }
}

std::string HttpServer::_readFile(const std::string& path) const
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return "";

    std::size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content(size, '\0');
    file.read(&content[0], size);
    file.close();

    return content;
}

std::string HttpServer::_resolvePath(const std::string& urlPrefix, const std::string& fsPath,
                                       const std::string& requestPath) const
{
    // Build: fsPath + requestPath
    std::string result = fsPath;
    if (result.back() != '/')
        result += '/';
    result += requestPath;

    // Canonicalize to prevent directory traversal
    try
    {
        std::filesystem::path p(result);
        std::filesystem::path base(fsPath);

        // Resolve to absolute and check if it's within base
        auto resolved = std::filesystem::canonical(p);
        auto baseCanonical = std::filesystem::canonical(base);

        // Check if resolved is under base
        auto relPath = std::filesystem::relative(resolved, baseCanonical);
        if (!relPath.string().empty() && relPath.string().find("..") == std::string::npos)
        {
            return resolved.string();
        }
    }
    catch (...)
    {
    }

    return ""; // Invalid or traversal attempt
}

} // namespace snf
