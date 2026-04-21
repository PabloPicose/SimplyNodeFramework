#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sockaddr_storage;

namespace snf {

enum class HostResolveMode {
    Connect,
    Bind,
};

class HostAddress final
{
public:
    explicit HostAddress(std::string host = {});

    static const HostAddress LocalHost;
    static const HostAddress LocalHostIPv6;
    static const HostAddress AnyIPv4;
    static const HostAddress AnyIPv6;

    const std::string& host() const;
    bool isEmpty() const;

    bool resolve(std::uint16_t port,
                 HostResolveMode mode,
                 std::vector<sockaddr_storage>& addresses,
                 std::string& errorMessage) const;

private:
    std::string m_host;
};

}  // namespace snf