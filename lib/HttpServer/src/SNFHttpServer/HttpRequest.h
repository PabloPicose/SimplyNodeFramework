#pragma once

/**
 * @file HttpRequest.h
 * @brief HTTP request representation.
 * @ingroup SNFHttpServer
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace snf {

/**
 * @class HttpRequest
 * @ingroup SNFHttpServer
 * @brief Represents a parsed HTTP request.
 *
 * Contains the HTTP method, request path, query parameters, headers, and body.
 * This is a value type (POD-like) that is populated by HttpRequestParser.
 */
class HttpRequest
{
public:
    /** @brief HTTP method (GET, POST, PUT, DELETE, etc.). */
    std::string method;

    /** @brief Request path (e.g., "/api/status"). */
    std::string path;

    /** @brief Query parameters parsed from the URL (e.g., ?a=1&b=2 → {a:1, b:2}). */
    std::map<std::string, std::string> queryParams;

    /** @brief HTTP headers as key-value pairs (case-insensitive keys). */
    std::map<std::string, std::string> headers;

    /** @brief Request body (empty for GET requests typically). */
    std::string body;

    /** @brief Returns the value of a header (case-insensitive), or empty string if not found. */
    std::string getHeader(const std::string& key) const;

    /** @brief Returns the value of a query parameter, or empty string if not found. */
    std::string getQueryParam(const std::string& key) const;

    /** @brief Returns the content type from the Content-Type header, or "text/plain" if not set. */
    std::string contentType() const;

    /** @brief Returns the content length from the Content-Length header, or 0 if not set or invalid. */
    std::size_t contentLength() const;
};

} // namespace snf
