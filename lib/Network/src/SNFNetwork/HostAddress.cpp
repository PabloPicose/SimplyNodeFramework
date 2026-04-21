#include "SNFNetwork/HostAddress.h"

#include <netdb.h>
#include <sys/socket.h>

#include <cstring>

namespace snf {

const HostAddress HostAddress::LocalHost{"127.0.0.1"};
const HostAddress HostAddress::LocalHostIPv6{"::1"};
const HostAddress HostAddress::AnyIPv4{"0.0.0.0"};
const HostAddress HostAddress::AnyIPv6{"::"};

HostAddress::HostAddress(std::string host) : m_host(std::move(host)) {}

const std::string& HostAddress::host() const { return m_host; }

bool HostAddress::isEmpty() const { return m_host.empty(); }

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
}

}  // namespace snf