/**
 * @file SnmpOid.cpp
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpOid.h"

#include <algorithm>
#include <sstream>

namespace snf {

SnmpOid::SnmpOid(const std::string& dotted)
{
    if (dotted.empty())
        return;

    std::istringstream ss(dotted);
    std::string token;
    while (std::getline(ss, token, '.')) {
        if (token.empty())
            continue;
        m_components.push_back(static_cast<std::uint32_t>(std::stoul(token)));
    }
}

SnmpOid::SnmpOid(std::vector<std::uint32_t> components)
    : m_components(std::move(components))
{
}

std::string SnmpOid::toString() const
{
    if (m_components.empty())
        return {};

    std::string result;
    result.reserve(m_components.size() * 4);
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (i != 0)
            result += '.';
        result += std::to_string(m_components[i]);
    }
    return result;
}

const std::vector<std::uint32_t>& SnmpOid::components() const noexcept
{
    return m_components;
}

bool SnmpOid::isValid() const noexcept
{
    return !m_components.empty();
}

bool SnmpOid::operator==(const SnmpOid& other) const noexcept
{
    return m_components == other.m_components;
}

bool SnmpOid::operator!=(const SnmpOid& other) const noexcept
{
    return !(*this == other);
}

bool SnmpOid::operator<(const SnmpOid& other) const noexcept
{
    return m_components < other.m_components;
}

bool SnmpOid::isPrefixOf(const SnmpOid& other) const noexcept
{
    if (m_components.size() > other.m_components.size())
        return false;
    return std::equal(m_components.begin(), m_components.end(),
                      other.m_components.begin());
}

} // namespace snf
