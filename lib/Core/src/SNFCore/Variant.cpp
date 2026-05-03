#include "SNFCore/Variant.h"

#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace snf {

// ── Constructors ──────────────────────────────────────────────────────────────

Variant::Variant(bool value) noexcept
    : m_value(value)
{
}

Variant::Variant(int value) noexcept
    : m_value(static_cast<std::int64_t>(value))
{
}

Variant::Variant(std::int64_t value) noexcept
    : m_value(value)
{
}

Variant::Variant(double value) noexcept
    : m_value(value)
{
}

Variant::Variant(std::string value)
    : m_value(std::move(value))
{
}

Variant::Variant(const char* value)
    : m_value(value ? std::string(value) : std::string())
{
}

Variant::Variant(ModelColor value) noexcept
    : m_value(value)
{
}

// ── Type query ────────────────────────────────────────────────────────────────

Variant::Type Variant::type() const noexcept
{
    return std::visit(
        [](const auto& v) -> Type {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) return Type::Null;
            if constexpr (std::is_same_v<T, bool>) return Type::Bool;
            if constexpr (std::is_same_v<T, std::int64_t>) return Type::Int64;
            if constexpr (std::is_same_v<T, double>) return Type::Double;
            if constexpr (std::is_same_v<T, std::string>) return Type::String;
            if constexpr (std::is_same_v<T, ModelColor>) return Type::Color;
        },
        m_value);
}

bool Variant::isNull() const noexcept
{
    return holds<std::monostate>();
}

bool Variant::isValid() const noexcept
{
    return ! isNull();
}

// ── Conversion helpers ────────────────────────────────────────────────────────

bool Variant::toBool(bool defaultValue) const noexcept
{
    return std::visit(
        [defaultValue](const auto& v) -> bool {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return v;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return v != 0;
            } else if constexpr (std::is_same_v<T, double>) {
                return v != 0.0;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v == "true" || v == "1";
            } else {
                return defaultValue;
            }
        },
        m_value);
}

std::int64_t Variant::toInt64(std::int64_t defaultValue) const noexcept
{
    return std::visit(
        [defaultValue](const auto& v) -> std::int64_t {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::int64_t>) {
                return v;
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? 1 : 0;
            } else if constexpr (std::is_same_v<T, double>) {
                return static_cast<std::int64_t>(v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                try {
                    return std::stoll(v);
                } catch (...) {
                    return defaultValue;
                }
            } else {
                return defaultValue;
            }
        },
        m_value);
}

double Variant::toDouble(double defaultValue) const noexcept
{
    return std::visit(
        [defaultValue](const auto& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) {
                return v;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return static_cast<double>(v);
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? 1.0 : 0.0;
            } else if constexpr (std::is_same_v<T, std::string>) {
                try {
                    return std::stod(v);
                } catch (...) {
                    return defaultValue;
                }
            } else {
                return defaultValue;
            }
        },
        m_value);
}

std::string Variant::toString() const
{
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream stream;
                stream << v;
                return stream.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else if constexpr (std::is_same_v<T, ModelColor>) {
                return {};
            }
        },
        m_value);
}

ModelColor Variant::toColor() const noexcept
{
    if (const auto* c = get<ModelColor>()) {
        return *c;
    }
    return {};
}

// ── Comparison ────────────────────────────────────────────────────────────────

bool Variant::operator==(const Variant& other) const
{
    return m_value == other.m_value;
}

bool Variant::operator!=(const Variant& other) const
{
    return ! (*this == other);
}

}  // namespace snf
