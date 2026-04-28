#include "SNFHttpServer/HttpRequestParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace snf {

HttpRequestParser::HttpRequestParser() = default;

void HttpRequestParser::feed(const uint8_t* data, std::size_t size)
{
    if (hasError())
        return;

    _buffer.append(reinterpret_cast<const char*>(data), size);

    if (_state == State::RequestLine)
    {
        _parseRequestLine();
    }

    if (_state == State::Headers && !hasError())
    {
        _parseHeaders();
    }

    if (_state == State::Body && !hasError())
    {
        _parseBody();
    }
}

void HttpRequestParser::feed(const std::string& data)
{
    feed(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool HttpRequestParser::isComplete() const
{
    return _state == State::Complete;
}

bool HttpRequestParser::hasError() const
{
    return _state == State::Error;
}

std::string HttpRequestParser::error() const
{
    return _errorMessage;
}

HttpRequest HttpRequestParser::parse()
{
    if (!isComplete())
        return HttpRequest();

    HttpRequest result = _request;
    reset();
    return result;
}

void HttpRequestParser::reset()
{
    _state = State::RequestLine;
    _buffer.clear();
    _errorMessage.clear();
    _request = HttpRequest();
    _bodyBytesRead = 0;
    _bodyBytesExpected = 0;
    _isChunked = false;
}

void HttpRequestParser::_parseRequestLine()
{
    std::size_t pos = _buffer.find("\r\n");
    if (pos == std::string::npos)
        return; // Wait for more data

    std::string line = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 2);

    // Parse: METHOD PATH HTTP/1.1
    std::istringstream iss(line);
    std::string method, path, protocol;

    if (!(iss >> method >> path >> protocol))
    {
        _state = State::Error;
        _errorMessage = "Invalid request line format";
        return;
    }

    // Normalize method to uppercase
    std::transform(method.begin(), method.end(), method.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    _request.method = method;

    // Parse query string from path
    std::size_t queryPos = path.find('?');
    if (queryPos != std::string::npos)
    {
        _request.path = path.substr(0, queryPos);
        std::string queryStr = path.substr(queryPos + 1);

        // Parse key=value&key=value
        std::istringstream queryStream(queryStr);
        std::string param;
        while (std::getline(queryStream, param, '&'))
        {
            std::size_t eqPos = param.find('=');
            if (eqPos != std::string::npos)
            {
                std::string key = param.substr(0, eqPos);
                std::string value = param.substr(eqPos + 1);
                _request.queryParams[key] = value;
            }
            else
            {
                _request.queryParams[param] = "";
            }
        }
    }
    else
    {
        _request.path = path;
    }

    // Validate protocol
    if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0")
    {
        _state = State::Error;
        _errorMessage = "Unsupported HTTP protocol version";
        return;
    }

    _state = State::Headers;
}

void HttpRequestParser::_parseHeaders()
{
    while (true)
    {
        std::size_t pos = _buffer.find("\r\n");
        if (pos == std::string::npos)
            return; // Wait for more data

        std::string line = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 2);

        if (line.empty())
        {
            // End of headers
            _state = State::Body;

            // Determine body size and format
            std::string transferEncoding = _request.getHeader("Transfer-Encoding");
            std::string contentLengthStr = _request.getHeader("Content-Length");

            if (!transferEncoding.empty())
            {
                // Simple check for chunked
                if (transferEncoding.find("chunked") != std::string::npos)
                {
                    _isChunked = true;
                    _bodyBytesExpected = 0; // Will be determined as we parse chunks
                }
            }
            else if (!contentLengthStr.empty())
            {
                _bodyBytesExpected = _parseContentLength(contentLengthStr);
            }

            // If no body expected, we're done
            if (_bodyBytesExpected == 0 && !_isChunked)
            {
                _state = State::Complete;
            }
            return;
        }

        // Parse header line: "Key: Value"
        std::size_t colonPos = line.find(':');
        if (colonPos == std::string::npos)
        {
            _state = State::Error;
            _errorMessage = "Invalid header format (missing colon)";
            return;
        }

        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);

        // Trim leading/trailing whitespace from value
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        _request.headers[key] = value;
    }
}

void HttpRequestParser::_parseBody()
{
    if (!_isChunked)
    {
        // Content-Length based body
        if (_bodyBytesRead < _bodyBytesExpected)
        {
            std::size_t bytesNeeded = _bodyBytesExpected - _bodyBytesRead;
            std::size_t bytesAvailable = _buffer.size();

            if (bytesAvailable >= bytesNeeded)
            {
                _request.body.append(_buffer.substr(0, bytesNeeded));
                _buffer.erase(0, bytesNeeded);
                _bodyBytesRead = _bodyBytesExpected;
                _state = State::Complete;
            }
            else
            {
                _request.body.append(_buffer);
                _bodyBytesRead += bytesAvailable;
                _buffer.clear();
            }
        }
    }
    else
    {
        // Chunked Transfer-Encoding (simplified: just read until we get a 0 chunk size)
        // This is a basic implementation; a full one would need better state management
        while (true)
        {
            std::size_t crlfPos = _buffer.find("\r\n");
            if (crlfPos == std::string::npos)
                return; // Wait for more data

            std::string chunkSizeLine = _buffer.substr(0, crlfPos);
            _buffer.erase(0, crlfPos + 2);

            // Parse hex chunk size
            unsigned long chunkSize = 0;
            try
            {
                chunkSize = std::stoul(chunkSizeLine, nullptr, 16);
            }
            catch (...)
            {
                _state = State::Error;
                _errorMessage = "Invalid chunk size";
                return;
            }

            if (chunkSize == 0)
            {
                // Last chunk; we're done
                _state = State::Complete;
                // Discard trailing headers/CRLF if present
                return;
            }

            // Read the chunk data
            if (_buffer.size() < chunkSize + 2) // +2 for CRLF after chunk
                return; // Wait for more data

            _request.body.append(_buffer.substr(0, chunkSize));
            _buffer.erase(0, chunkSize + 2); // Also remove the trailing CRLF
        }
    }
}

std::size_t HttpRequestParser::_parseContentLength(const std::string& headerValue) const
{
    try
    {
        return std::stoul(headerValue);
    }
    catch (...)
    {
        return 0;
    }
}

} // namespace snf
