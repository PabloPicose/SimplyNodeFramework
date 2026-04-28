#pragma once

/**
 * @file HttpRequestParser.h
 * @brief HTTP/1.1 request parser with stateful incremental parsing.
 * @ingroup SNFHttpServer
 */

#include "SNFHttpServer/HttpRequest.h"

#include <cstddef>
#include <string>

namespace snf {

/**
 * @class HttpRequestParser
 * @ingroup SNFHttpServer
 * @brief Stateful parser for HTTP/1.1 requests.
 *
 * Parses incoming HTTP data incrementally using `feed()`. Call `isComplete()`
 * to check if a full request has been parsed. Call `parse()` to retrieve the
 * parsed `HttpRequest`. If parsing fails, `error()` returns a description.
 *
 * The parser handles:
 * - Request line (METHOD PATH HTTP/1.1)
 * - Headers (key: value, until blank line)
 * - Query string parsing (from path)
 * - Content-Length bodies
 * - Chunked Transfer-Encoding (basic support)
 *
 * After a complete request is parsed and `parse()` is called, the parser
 * resets for the next request.
 *
 * @code
 * HttpRequestParser parser;
 * while (hasData()) {
 *     std::vector<uint8_t> chunk = recv();
 *     parser.feed(chunk);
 *     if (parser.isComplete()) {
 *         HttpRequest req = parser.parse();
 *         handleRequest(req);
 *     }
 * }
 * @endcode
 */
class HttpRequestParser
{
public:
    /** @brief Constructs an empty parser ready to receive data. */
    HttpRequestParser();

    /**
     * @brief Feeds a chunk of raw HTTP data to the parser.
     * @param data Raw bytes from socket.
     * @param size Number of bytes in @p data.
     * @details Updates internal state. Does nothing if an error has occurred.
     */
    void feed(const uint8_t* data, std::size_t size);

    /** @brief Convenience overload: feeds a string. */
    void feed(const std::string& data);

    /**
     * @brief Returns `true` if a complete HTTP request has been parsed.
     *        Call `parse()` to retrieve the request.
     */
    bool isComplete() const;

    /**
     * @brief Returns `true` if a parsing error has occurred.
     *        Call `error()` to retrieve the error description.
     */
    bool hasError() const;

    /**
     * @brief Returns the error description if `hasError()` is true.
     *        Empty string otherwise.
     */
    std::string error() const;

    /**
     * @brief Retrieves the parsed `HttpRequest` and resets the parser.
     * @pre `isComplete()` must be true.
     * @return The parsed request.
     */
    HttpRequest parse();

    /**
     * @brief Manually resets the parser to initial state (clears any partial data).
     */
    void reset();

private:
    enum class State
    {
        RequestLine,    // Parsing "METHOD PATH HTTP/1.1"
        Headers,        // Parsing header lines
        Body,           // Reading the body
        Complete,       // Request fully parsed
        Error           // Parsing error
    };

    State _state = State::RequestLine;
    std::string _buffer;
    std::string _errorMessage;

    HttpRequest _request;
    std::size_t _bodyBytesRead = 0;
    std::size_t _bodyBytesExpected = 0;
    bool _isChunked = false;

    // Parsing helpers
    void _parseRequestLine();
    void _parseHeaders();
    void _parseBody();
    std::size_t _parseContentLength(const std::string& headerValue) const;
};

} // namespace snf
