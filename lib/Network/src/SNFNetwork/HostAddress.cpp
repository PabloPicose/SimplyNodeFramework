#include "SNFNetwork/HostAddress.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#if !defined(__EMSCRIPTEN__)
#include <netdb.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>

namespace snf {

namespace {
bool isAsciiAlphaNumeric(char ch)
{
    const auto value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0;
}

bool isValidHostnameLabel(const std::string& host, std::size_t begin, std::size_t end)
{
    const std::size_t length = end - begin;
    if (length == 0 || length > 63) {
        return false;
    }

    if (! isAsciiAlphaNumeric(host[begin]) || ! isAsciiAlphaNumeric(host[end - 1])) {
        return false;
    }

    for (std::size_t i = begin; i < end; ++i) {
        const char ch = host[i];
        if (! isAsciiAlphaNumeric(ch) && ch != '-') {
            return false;
        }
    }

    return true;
}

bool isValidIpLiteral(const std::string& host)
{
    in_addr ipv4{};
    if (::inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
        return true;
    }

    in6_addr ipv6{};
    return ::inet_pton(AF_INET6, host.c_str(), &ipv6) == 1;
}

bool isValidDnsHostname(const std::string& host)
{
    if (host.empty() || host.size() > 253) {
        return false;
    }

    const bool looksLikeMalformedIpv4 =
        host.find('.') != std::string::npos &&
        std::all_of(host.begin(), host.end(), [](char ch) {
            const auto value = static_cast<unsigned char>(ch);
            return std::isdigit(value) != 0 || ch == '.';
        });
    if (looksLikeMalformedIpv4) {
        return false;
    }

    if (host.front() == '.' || host.front() == '-' || host.front() == '[' || host.front() == ']') {
        return false;
    }

    const std::size_t effectiveSize = host.back() == '.' ? host.size() - 1 : host.size();
    if (effectiveSize == 0) {
        return false;
    }

    std::size_t labelBegin = 0;
    while (labelBegin < effectiveSize) {
        const std::size_t labelEnd = host.find('.', labelBegin);
        const std::size_t end = labelEnd == std::string::npos || labelEnd > effectiveSize
            ? effectiveSize
            : labelEnd;

        if (! isValidHostnameLabel(host, labelBegin, end)) {
            return false;
        }

        labelBegin = end + 1;
    }

    return true;
}

#if defined(__EMSCRIPTEN__)
bool appendNumericAddress(const std::string& host, std::uint16_t port, std::vector<sockaddr_storage>& addresses)
{
    sockaddr_storage storage{};

    auto* ipv4 = reinterpret_cast<sockaddr_in*>(&storage);
    ipv4->sin_family = AF_INET;
    ipv4->sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &ipv4->sin_addr) == 1) {
        addresses.push_back(storage);
        return true;
    }

    storage = sockaddr_storage{};
    auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&storage);
    ipv6->sin6_family = AF_INET6;
    ipv6->sin6_port = htons(port);
    if (::inet_pton(AF_INET6, host.c_str(), &ipv6->sin6_addr) == 1) {
        addresses.push_back(storage);
        return true;
    }

    return false;
}

void appendPassiveWildcardAddresses(std::uint16_t port, std::vector<sockaddr_storage>& addresses)
{
    sockaddr_storage ipv4Storage{};
    auto* ipv4 = reinterpret_cast<sockaddr_in*>(&ipv4Storage);
    ipv4->sin_family = AF_INET;
    ipv4->sin_port = htons(port);
    ipv4->sin_addr.s_addr = htonl(INADDR_ANY);
    addresses.push_back(ipv4Storage);

    sockaddr_storage ipv6Storage{};
    auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&ipv6Storage);
    ipv6->sin6_family = AF_INET6;
    ipv6->sin6_port = htons(port);
    ipv6->sin6_addr = in6addr_any;
    addresses.push_back(ipv6Storage);
}
#endif
}  // namespace

const HostAddress HostAddress::LocalHost{"127.0.0.1"};
const HostAddress HostAddress::LocalHostIPv6{"::1"};
const HostAddress HostAddress::AnyIPv4{"0.0.0.0"};
const HostAddress HostAddress::AnyIPv6{"::"};

HostAddress::HostAddress(std::string host) : m_host(std::move(host)) {}

const std::string& HostAddress::host() const { return m_host; }

std::string HostAddress::toString() const { return m_host; }

bool HostAddress::isEmpty() const { return m_host.empty(); }

bool HostAddress::isValid() const { return isValidHost(m_host); }

bool HostAddress::isValidHost(const std::string& host)
{
    return isValidIpLiteral(host) || isValidDnsHostname(host);
}

bool HostAddress::resolve(std::uint16_t port,
                          HostResolveMode mode,
                          std::vector<sockaddr_storage>& addresses,
                          std::string& errorMessage) const
{
    addresses.clear();
    errorMessage.clear();

    if (mode == HostResolveMode::Connect && m_host.empty()) {
        errorMessage = "Host is empty";
        return false;
    }

#if defined(__EMSCRIPTEN__)
    if (mode == HostResolveMode::Bind && m_host.empty()) {
        appendPassiveWildcardAddresses(port, addresses);
        return true;
    }

    if (appendNumericAddress(m_host, port, addresses)) {
        return true;
    }

    errorMessage = "Host name resolution is not available in WebAssembly builds. Browser WebSocket APIs resolve "
                   "hostnames from the URL; use resolve() only for numeric IP literals in this build.";
    return false;
#else
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = mode == HostResolveMode::Bind ? AI_PASSIVE : 0;

    const std::string service = std::to_string(port);
    const char* node = m_host.empty() ? nullptr : m_host.c_str();

    addrinfo* result = nullptr;
    const int rc = ::getaddrinfo(node, service.c_str(), &hints, &result);
    if (rc != 0) {
        errorMessage = ::gai_strerror(rc);
        return false;
    }

    for (const addrinfo* it = result; it != nullptr; it = it->ai_next) {
        if (it->ai_addr == nullptr || it->ai_addrlen <= 0) {
            continue;
        }

        sockaddr_storage storage{};
        const std::size_t copySize =
            static_cast<std::size_t>(it->ai_addrlen) < sizeof(storage)
                ? static_cast<std::size_t>(it->ai_addrlen)
                : sizeof(storage);
        std::memcpy(&storage, it->ai_addr, copySize);
        addresses.push_back(storage);
    }

    ::freeaddrinfo(result);

    if (addresses.empty()) {
        errorMessage = "No address candidates";
        return false;
    }

    return true;
#endif
}

}  // namespace snf
