/**
 * @file SnmpValue.cpp
 * @ingroup SNFSnmp
 */

#include "SNFSnmp/SnmpValue.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace snf {

// ── Factory methods ────────────────────────────────────────────────────────

SnmpValue SnmpValue::fromInteger32(std::int32_t v)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::Integer32;
    val.m_storage = v;
    return val;
}

SnmpValue SnmpValue::fromCounter32(std::uint32_t v)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::Counter32;
    val.m_storage = v;
    return val;
}

SnmpValue SnmpValue::fromGauge32(std::uint32_t v)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::Gauge32;
    val.m_storage = v;
    return val;
}

SnmpValue SnmpValue::fromTimeTicks(std::uint32_t v)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::TimeTicks;
    val.m_storage = v;
    return val;
}

SnmpValue SnmpValue::fromCounter64(std::uint64_t v)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::Counter64;
    val.m_storage = v;
    return val;
}

SnmpValue SnmpValue::fromOctetString(std::vector<std::uint8_t> bytes)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::OctetString;
    val.m_storage = std::move(bytes);
    return val;
}

SnmpValue SnmpValue::fromOctetString(const std::string& text)
{
    return fromOctetString(std::vector<std::uint8_t>(text.begin(), text.end()));
}

SnmpValue SnmpValue::fromIpAddress(const std::string& dotted)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::IpAddress;
    val.m_storage = dotted;
    return val;
}

SnmpValue SnmpValue::fromObjectIdentifier(SnmpOid oid)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::ObjectIdentifier;
    val.m_storage = std::move(oid);
    return val;
}

SnmpValue SnmpValue::fromOpaque(std::vector<std::uint8_t> bytes)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::Opaque;
    val.m_storage = std::move(bytes);
    return val;
}

SnmpValue SnmpValue::fromFloat(float v)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::Float;
    val.m_storage = v;
    return val;
}

SnmpValue SnmpValue::fromDouble(double v)
{
    SnmpValue val;
    val.m_type    = SnmpValueType::Double;
    val.m_storage = v;
    return val;
}

SnmpValue SnmpValue::fromBool(bool v)
{
    // TruthValue per RFC 2579: true(1), false(2).
    return fromInteger32(v ? 1 : 2);
}

// ── Type ──────────────────────────────────────────────────────────────────

SnmpValueType SnmpValue::type() const noexcept
{
    return m_type;
}

// ── Typed accessors ───────────────────────────────────────────────────────

std::int32_t SnmpValue::toInt32() const
{
    return std::get<std::int32_t>(m_storage);
}

std::uint32_t SnmpValue::toUint32() const
{
    return std::get<std::uint32_t>(m_storage);
}

std::uint64_t SnmpValue::toCounter64() const
{
    return std::get<std::uint64_t>(m_storage);
}

std::string SnmpValue::toString() const
{
    if (m_type == SnmpValueType::IpAddress)
        return std::get<std::string>(m_storage);

    // OctetString: reinterpret as UTF-8 string
    const auto& bytes = std::get<std::vector<std::uint8_t>>(m_storage);
    return std::string(bytes.begin(), bytes.end());
}

const std::vector<std::uint8_t>& SnmpValue::toBytes() const
{
    return std::get<std::vector<std::uint8_t>>(m_storage);
}

const SnmpOid& SnmpValue::toOid() const
{
    return std::get<SnmpOid>(m_storage);
}

float SnmpValue::toFloat() const
{
    return std::get<float>(m_storage);
}

double SnmpValue::toDouble() const
{
    return std::get<double>(m_storage);
}

bool SnmpValue::toBool() const
{
    // TruthValue per RFC 2579: true == 1.
    return std::get<std::int32_t>(m_storage) == 1;
}

std::string SnmpValue::toDisplayString() const
{
    switch (m_type) {
        case SnmpValueType::Null:
            return "(null)";
        case SnmpValueType::Integer32:
            return std::to_string(std::get<std::int32_t>(m_storage));
        case SnmpValueType::Counter32:
            return "Counter32(" + std::to_string(std::get<std::uint32_t>(m_storage)) + ")";
        case SnmpValueType::Gauge32:
            return "Gauge32(" + std::to_string(std::get<std::uint32_t>(m_storage)) + ")";
        case SnmpValueType::TimeTicks: {
            const std::uint32_t ticks = std::get<std::uint32_t>(m_storage);
            const std::uint32_t centisecs = ticks % 100;
            const std::uint32_t secs      = (ticks / 100) % 60;
            const std::uint32_t mins      = (ticks / 6000) % 60;
            const std::uint32_t hours     = (ticks / 360000) % 24;
            const std::uint32_t days      = ticks / 8640000;
            std::ostringstream oss;
            oss << "TimeTicks(" << days << "d " << hours << "h " << mins << "m "
                << secs << "." << centisecs << "s)";
            return oss.str();
        }
        case SnmpValueType::Counter64:
            return "Counter64(" + std::to_string(std::get<std::uint64_t>(m_storage)) + ")";
        case SnmpValueType::OctetString: {
            const auto& bytes = std::get<std::vector<std::uint8_t>>(m_storage);
            // Check if printable ASCII
            bool printable = true;
            for (auto b : bytes) {
                if (b < 0x20 || b > 0x7e) { printable = false; break; }
            }
            if (printable)
                return '"' + std::string(bytes.begin(), bytes.end()) + '"';
            // Fall back to hex
            std::ostringstream oss;
            oss << std::hex;
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                if (i) oss << ':';
                oss << static_cast<int>(bytes[i]);
            }
            return oss.str();
        }
        case SnmpValueType::IpAddress:
            return std::get<std::string>(m_storage);
        case SnmpValueType::ObjectIdentifier:
            return std::get<SnmpOid>(m_storage).toString();
        case SnmpValueType::Opaque: {
            const auto& bytes = std::get<std::vector<std::uint8_t>>(m_storage);
            std::ostringstream oss;
            oss << "Opaque[" << std::hex;
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                if (i) oss << ' ';
                oss << static_cast<int>(bytes[i]);
            }
            oss << ']';
            return oss.str();
        }
        case SnmpValueType::Float: {
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<float>::max_digits10)
                << std::get<float>(m_storage);
            return oss.str();
        }
        case SnmpValueType::Double: {
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<double>::max_digits10)
                << std::get<double>(m_storage);
            return oss.str();
        }
    }
    return {};
}

// ── net-snmp interop ──────────────────────────────────────────────────────

char SnmpValue::netSnmpTypeChar() const noexcept
{
    switch (m_type) {
        case SnmpValueType::Integer32:        return 'i';
        case SnmpValueType::Counter32:        return 'c';
        case SnmpValueType::Gauge32:          return 'u';
        case SnmpValueType::TimeTicks:        return 't';
        case SnmpValueType::Counter64:        return 'C';
        case SnmpValueType::OctetString:      return 's';
        case SnmpValueType::IpAddress:        return 'a';
        case SnmpValueType::ObjectIdentifier: return 'o';
        case SnmpValueType::Opaque:           return 'x'; // hex string
        case SnmpValueType::Float:            return 'F';
        case SnmpValueType::Double:           return 'D';
        default:                              return 'n'; // null
    }
}

std::string SnmpValue::toNetSnmpString() const
{
    switch (m_type) {
        case SnmpValueType::Integer32:
            return std::to_string(std::get<std::int32_t>(m_storage));
        case SnmpValueType::Counter32:
        case SnmpValueType::Gauge32:
        case SnmpValueType::TimeTicks:
            return std::to_string(std::get<std::uint32_t>(m_storage));
        case SnmpValueType::Counter64:
            return std::to_string(std::get<std::uint64_t>(m_storage));
        case SnmpValueType::OctetString: {
            const auto& bytes = std::get<std::vector<std::uint8_t>>(m_storage);
            return std::string(bytes.begin(), bytes.end());
        }
        case SnmpValueType::IpAddress:
            return std::get<std::string>(m_storage);
        case SnmpValueType::ObjectIdentifier:
            return std::get<SnmpOid>(m_storage).toString();
        case SnmpValueType::Opaque: {
            // Encode as hex string for 'x' type
            const auto& bytes = std::get<std::vector<std::uint8_t>>(m_storage);
            std::ostringstream oss;
            oss << std::hex;
            for (auto b : bytes)
                oss << static_cast<int>(b);
            return oss.str();
        }
        case SnmpValueType::Float: {
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<float>::max_digits10)
                << std::get<float>(m_storage);
            return oss.str();
        }
        case SnmpValueType::Double: {
            std::ostringstream oss;
            oss << std::setprecision(std::numeric_limits<double>::max_digits10)
                << std::get<double>(m_storage);
            return oss.str();
        }
        default:
            return {};
    }
}

} // namespace snf
