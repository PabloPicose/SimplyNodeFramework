#pragma once

/**
 * @file HttpResponse.h
 * @brief HTTP response representation and helpers.
 * @ingroup SNFHttpServer
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace snf {

/**
 * @class HttpResponse
 * @ingroup SNFHttpServer
 * @brief Represents an HTTP response to be sent back to the client.
 *
 * Includes status code, headers, and body. Provides helper methods to
 * construct common response types (JSON, text, HTML, file).
 */
class HttpResponse
{
public:
    /** @brief Default constructor initializes a 200 OK text/plain response. */
    HttpResponse();

    /** @brief Explicit status code and optional body. */
    explicit HttpResponse(int statusCode, const std::string& body = "");

    /** @brief HTTP status code (e.g., 200, 404, 500). */
    int statusCode = 200;

    /** @brief Response headers as key-value pairs. */
    std::map<std::string, std::string> headers;

    /** @brief Response body (raw bytes). */
    std::string body;

    /**
     * @brief Sets the response body to @p data and content type to "application/json".
     * @return Reference to *this for chaining.
     */
    HttpResponse& json(const std::string& data);

    /**
     * @brief Sets the response body to @p text and content type to "text/plain".
     * @return Reference to *this for chaining.
     */
    HttpResponse& text(const std::string& text);

    /**
     * @brief Sets the response body to @p html and content type to "text/html".
     * @return Reference to *this for chaining.
     */
    HttpResponse& html(const std::string& html);

    /**
     * @brief Sets the response body from the contents of @p filePath and infers MIME type.
     * @details If the file cannot be read, status is set to 500 Internal Server Error.
     * @return Reference to *this for chaining.
     */
    HttpResponse& file(const std::string& filePath);

    /**
     * @brief Sets the HTTP status code (e.g., 404, 500).
     * @return Reference to *this for chaining.
     */
    HttpResponse& status(int code);

    /**
     * @brief Sets a custom header value.
     * @return Reference to *this for chaining.
     */
    HttpResponse& header(const std::string& key, const std::string& value);

    /**
     * @brief Serializes the response to HTTP/1.1 wire format.
     * @details Returns the complete response including status line, all headers (CRLF-terminated),
     *          blank line, and body.
     */
    std::string serialize() const;

    /**
     * @brief Factory: creates a 404 Not Found response with default body.
     */
    static HttpResponse notFound();

    /**
     * @brief Factory: creates a 405 Method Not Allowed response with default body.
     */
    static HttpResponse methodNotAllowed();

    /**
     * @brief Factory: creates a 400 Bad Request response with default body.
     */
    static HttpResponse badRequest(const std::string& reason = "Bad Request");

    /**
     * @brief Factory: creates a 500 Internal Server Error response with default body.
     */
    static HttpResponse internalServerError(const std::string& reason = "Internal Server Error");

private:
    /**
     * @brief Internal helper to set content type and body together.
     */
    HttpResponse& _setBodyAndContentType(const std::string& body, const std::string& contentType);
    
    /**
     * @brief Guess MIME type from file path extension.
     */
    std::string _guessContentType(const std::string& filePath) const;
};

} // namespace snf
