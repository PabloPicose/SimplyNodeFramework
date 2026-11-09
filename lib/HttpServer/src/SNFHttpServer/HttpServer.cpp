#include "SNFHttpServer/HttpServer.h"

#include "SNFNetwork/HostAddress.h"
#include "SNFNetwork/TcpServer.h"
#include "SNFNetwork/TcpSocket.h"
#include "SNFHttpServer/HttpRequestParser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace snf {

namespace {

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool requestsWebSocketUpgrade(const HttpRequest& request)
{
    // "Upgrade" is a comma-separated list of protocol tokens.
    std::stringstream tokens(toLowerAscii(request.getHeader("Upgrade")));
    std::string token;
    while (std::getline(tokens, token, ','))
    {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (token == "websocket")
            return true;
    }
    return false;
}

HttpResponse upgradeRequired()
{
    HttpResponse resp(426);
    resp.body = "426 Upgrade Required";
    resp.headers["Content-Type"] = "text/plain";
    resp.headers["Upgrade"] = "websocket";
    resp.headers["Connection"] = "Upgrade, close";
    return resp;
}

} // namespace

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

void HttpServer::upgrade(const std::string& path, const UpgradeHandler& handler)
{
    _upgradeRoutes[path] = handler;
}

bool HttpServer::listen(const HostAddress& host, std::uint16_t port)
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
        on_error.emit("Failed to bind to " + host.host() + ":" + std::to_string(port));
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
        SocketState& state = _socketStates[socket];

        // Connect signals; the handles are kept so an upgrade hand-off can
        // drop them and leave the socket free of HttpServer slots.
        state.slots.push_back(
            socket->readyRead.connect([this, socket]() { _onSocketReadyRead(socket); }));
        state.slots.push_back(
            socket->disconnected.connect([this, socket]() { _closeSocket(socket); }));
        state.slots.push_back(socket->errorOccurred.connect(
            [this, socket](const std::string& msg) { _onSocketError(socket, msg); }));
        state.slots.push_back(socket->bytesWritten.connect([this, socket](std::size_t n) {
            auto stateIt = _socketStates.find(socket);
            if (stateIt == _socketStates.end())
                return;
            SocketState& state = stateIt->second;
            state.totalSent += n;
            if (state.pendingClose && state.totalSent >= state.totalToSend)
                _closeSocket(socket);
        }));
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

    SocketState& socketState = stateIt->second;
    if (socketState.pendingClose)
        return; // Response already queued; ignore anything else the client sends.

    HttpRequestParser& parser = socketState.parser;

    // Keep a raw copy only when an upgrade hand-off may need it.
    if (!_upgradeRoutes.empty())
        socketState.raw.append(data.toString());

    // Feed data to parser
    parser.feed(data.bytesView());

    // Process any complete requests
    while (parser.isComplete())
    {
        HttpRequest request = parser.parse();

        HttpResponse response;

        const auto upgradeIt = _upgradeRoutes.find(request.path);
        const bool wantsUpgrade = requestsWebSocketUpgrade(request);
        if (upgradeIt != _upgradeRoutes.end())
        {
            if (wantsUpgrade && request.method == "GET")
            {
                _handOff(socket, upgradeIt->second);
                return;
            }
            _sendAndClose(socket, upgradeRequired());
            return;
        }
        if (wantsUpgrade)
        {
            _sendAndClose(socket, HttpResponse::notFound());
            return;
        }

        // Find matching route
        RouteKey key{request.method, request.path};
        auto routeIt = _routes.find(key);

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

        _sendAndClose(socket, response);
        return;
    }

    if (parser.hasError())
    {
        _sendAndClose(socket, HttpResponse::badRequest(parser.error()));
    }
}

void HttpServer::_sendAndClose(TcpSocket* socket, const HttpResponse& response)
{
    auto stateIt = _socketStates.find(socket);
    if (stateIt == _socketStates.end())
        return;

    std::string serialized = response.serialize();

    // Schedule close only after all bytes are confirmed flushed by bytesWritten signal.
    // For large files the kernel send buffer fills up and flushPendingWrites() returns
    // EAGAIN; closing immediately would discard the unsent bytes still in m_writeBuffer.
    SocketState& state = stateIt->second;
    state.pendingClose = true;
    state.totalToSend  = serialized.size();
    state.totalSent    = 0;
    state.raw.clear();

    socket->write(serialized);
}

void HttpServer::_handOff(TcpSocket* socket, const UpgradeHandler& handler)
{
    auto stateIt = _socketStates.find(socket);
    if (stateIt == _socketStates.end())
        return;

    // From here on the socket is the handler's: no HttpServer slot may touch it.
    for (Connection& slot : stateIt->second.slots)
        slot.disconnect();
    std::string alreadyRead = std::move(stateIt->second.raw);
    _socketStates.erase(stateIt);

    // Copy: the handler may re-register the route (replacing the stored function).
    UpgradeHandler callback = handler;
    if (!callback)
    {
        socket->close();
        socket->deleteLater();
        return;
    }
    callback(socket, std::move(alreadyRead));
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
