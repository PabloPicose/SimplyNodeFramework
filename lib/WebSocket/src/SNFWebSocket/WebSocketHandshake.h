#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace snf::websocket::detail {

struct HttpMessage {
    std::string startLine;
    std::map<std::string, std::string> headers;
};

std::string createClientKey();
std::string computeAcceptKey(const std::string& clientKey);

std::string buildClientHandshakeRequest(const std::string& host,
                                        std::uint16_t port,
                                        const std::string& path,
                                        const std::string& clientKey);

std::string buildServerHandshakeResponse(const std::string& clientKey);

bool parseHttpMessage(const std::string& raw, HttpMessage& message, std::string& errorMessage);

bool validateServerHandshakeResponse(const std::string& rawResponse,
                                     const std::string& expectedClientKey,
                                     std::string& errorMessage);

bool validateClientHandshakeRequest(const std::string& rawRequest,
                                    std::string& clientKey,
                                    std::string& errorMessage);

}  // namespace snf::websocket::detail
