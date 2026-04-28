#include "SNFHttpServer/HttpResponse.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace snf {

HttpResponse::HttpResponse()
    : statusCode(200)
{
    headers["Content-Type"] = "text/plain";
}

HttpResponse::HttpResponse(int statusCode, const std::string& body)
    : statusCode(statusCode), body(body)
{
    headers["Content-Type"] = "text/plain";
}

HttpResponse& HttpResponse::json(const std::string& data)
{
    return _setBodyAndContentType(data, "application/json");
}

HttpResponse& HttpResponse::text(const std::string& text)
{
    return _setBodyAndContentType(text, "text/plain");
}

HttpResponse& HttpResponse::html(const std::string& html)
{
    return _setBodyAndContentType(html, "text/html");
}

HttpResponse& HttpResponse::file(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        statusCode = 500;
        body = "Internal Server Error: Cannot read file";
        headers["Content-Type"] = "text/plain";
        return *this;
    }

    std::size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    body.resize(size);
    file.read(&body[0], size);
    file.close();

    statusCode = 200;
    headers["Content-Type"] = _guessContentType(filePath);
    return *this;
}

HttpResponse& HttpResponse::status(int code)
{
    statusCode = code;
    return *this;
}

HttpResponse& HttpResponse::header(const std::string& key, const std::string& value)
{
    headers[key] = value;
    return *this;
}

std::string HttpResponse::serialize() const
{
    std::ostringstream oss;

    // Status line
    const char* statusText = "Unknown";
    if (statusCode == 200)
        statusText = "OK";
    else if (statusCode == 201)
        statusText = "Created";
    else if (statusCode == 204)
        statusText = "No Content";
    else if (statusCode == 400)
        statusText = "Bad Request";
    else if (statusCode == 404)
        statusText = "Not Found";
    else if (statusCode == 405)
        statusText = "Method Not Allowed";
    else if (statusCode == 411)
        statusText = "Length Required";
    else if (statusCode == 500)
        statusText = "Internal Server Error";
    else if (statusCode == 503)
        statusText = "Service Unavailable";

    oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";

    // Headers
    auto headersCopy = headers;
    headersCopy["Content-Length"] = std::to_string(body.size());

    for (const auto& [key, value] : headersCopy)
    {
        oss << key << ": " << value << "\r\n";
    }

    // Blank line
    oss << "\r\n";

    // Body
    oss << body;

    return oss.str();
}

HttpResponse HttpResponse::notFound()
{
    HttpResponse resp(404);
    resp.body = "404 Not Found";
    resp.headers["Content-Type"] = "text/plain";
    return resp;
}

HttpResponse HttpResponse::methodNotAllowed()
{
    HttpResponse resp(405);
    resp.body = "405 Method Not Allowed";
    resp.headers["Content-Type"] = "text/plain";
    return resp;
}

HttpResponse HttpResponse::badRequest(const std::string& reason)
{
    HttpResponse resp(400);
    resp.body = "400 Bad Request: " + reason;
    resp.headers["Content-Type"] = "text/plain";
    return resp;
}

HttpResponse HttpResponse::internalServerError(const std::string& reason)
{
    HttpResponse resp(500);
    resp.body = "500 Internal Server Error: " + reason;
    resp.headers["Content-Type"] = "text/plain";
    return resp;
}

HttpResponse& HttpResponse::_setBodyAndContentType(const std::string& body,
                                                   const std::string& contentType)
{
    this->body = body;
    headers["Content-Type"] = contentType;
    return *this;
}

std::string HttpResponse::_guessContentType(const std::string& filePath) const
{
    // Extract extension
    std::size_t dotPos = filePath.rfind('.');
    if (dotPos == std::string::npos)
        return "application/octet-stream";

    std::string ext = filePath.substr(dotPos);

    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Common MIME types
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

} // namespace snf
