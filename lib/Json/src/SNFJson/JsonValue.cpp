#include "SNFJson/JsonValue.h"

#include "SNFJson/JsonArray.h"
#include "SNFJson/JsonObject.h"
#include "SNFJson/JsonStorage.h"

#include <stdexcept>
#include <utility>

namespace snf {

JsonValue::JsonValue()
    : m_storage(detail::makeJsonStorage(nullptr))
{
}

JsonValue::JsonValue(const JsonValue& other)
    : m_storage(detail::makeJsonStorage(other.m_storage->value))
{
}

JsonValue& JsonValue::operator=(const JsonValue& other)
{
    if (this != &other) {
        m_storage = detail::makeJsonStorage(other.m_storage->value);
    }

    return *this;
}

JsonValue::JsonValue(std::nullptr_t)
    : m_storage(detail::makeJsonStorage(nullptr))
{
}

JsonValue::JsonValue(bool value)
    : m_storage(detail::makeJsonStorage(value))
{
}

JsonValue::JsonValue(int value)
    : m_storage(detail::makeJsonStorage(value))
{
}

JsonValue::JsonValue(long long value)
    : m_storage(detail::makeJsonStorage(value))
{
}

JsonValue::JsonValue(unsigned int value)
    : m_storage(detail::makeJsonStorage(value))
{
}

JsonValue::JsonValue(unsigned long long value)
    : m_storage(detail::makeJsonStorage(value))
{
}

JsonValue::JsonValue(double value)
    : m_storage(detail::makeJsonStorage(value))
{
}

JsonValue::JsonValue(const char* value)
    : m_storage(detail::makeJsonStorage(value == nullptr ? nlohmann::json(nullptr) : nlohmann::json(value)))
{
}

JsonValue::JsonValue(const std::string& value)
    : m_storage(detail::makeJsonStorage(value))
{
}

JsonValue::JsonValue(std::string_view value)
    : m_storage(detail::makeJsonStorage(std::string(value)))
{
}

JsonValue::JsonValue(const JsonObject& value)
    : m_storage(detail::makeJsonStorage(value.storage()->value))
{
}

JsonValue::JsonValue(const JsonArray& value)
    : m_storage(detail::makeJsonStorage(value.storage()->value))
{
}

JsonValue::JsonValue(std::shared_ptr<detail::JsonStorage> storage)
    : m_storage(std::move(storage))
{
}

JsonValue JsonValue::object()
{
    return JsonValue(detail::makeJsonStorage(nlohmann::json::object()));
}

JsonValue JsonValue::array()
{
    return JsonValue(detail::makeJsonStorage(nlohmann::json::array()));
}

JsonValue::Type JsonValue::type() const noexcept
{
    const auto& value = m_storage->value;
    if (value.is_null()) {
        return Type::Null;
    }
    if (value.is_boolean()) {
        return Type::Boolean;
    }
    if (value.is_number_integer()) {
        return Type::NumberInteger;
    }
    if (value.is_number_unsigned()) {
        return Type::NumberUnsigned;
    }
    if (value.is_number_float()) {
        return Type::NumberFloat;
    }
    if (value.is_string()) {
        return Type::String;
    }
    if (value.is_object()) {
        return Type::Object;
    }
    return Type::Array;
}

bool JsonValue::isNull() const noexcept
{
    return m_storage->value.is_null();
}

bool JsonValue::isBool() const noexcept
{
    return m_storage->value.is_boolean();
}

bool JsonValue::isNumber() const noexcept
{
    return m_storage->value.is_number();
}

bool JsonValue::isInteger() const noexcept
{
    return m_storage->value.is_number_integer();
}

bool JsonValue::isUnsigned() const noexcept
{
    return m_storage->value.is_number_unsigned();
}

bool JsonValue::isFloat() const noexcept
{
    return m_storage->value.is_number_float();
}

bool JsonValue::isString() const noexcept
{
    return m_storage->value.is_string();
}

bool JsonValue::isObject() const noexcept
{
    return m_storage->value.is_object();
}

bool JsonValue::isArray() const noexcept
{
    return m_storage->value.is_array();
}

bool JsonValue::asBool() const
{
    return m_storage->value.get<bool>();
}

int JsonValue::asInt() const
{
    return m_storage->value.get<int>();
}

long long JsonValue::asInt64() const
{
    return m_storage->value.get<long long>();
}

unsigned long long JsonValue::asUInt64() const
{
    return m_storage->value.get<unsigned long long>();
}

double JsonValue::asDouble() const
{
    return m_storage->value.get<double>();
}

std::string JsonValue::asString() const
{
    return m_storage->value.get<std::string>();
}

JsonObject JsonValue::asObject() const
{
    if (! m_storage->value.is_object()) {
        throw std::runtime_error("JSON value is not an object");
    }

    return JsonObject(detail::makeJsonStorage(m_storage->value));
}

JsonArray JsonValue::asArray() const
{
    if (! m_storage->value.is_array()) {
        throw std::runtime_error("JSON value is not an array");
    }

    return JsonArray(detail::makeJsonStorage(m_storage->value));
}

std::string JsonValue::toString() const
{
    return m_storage->value.dump();
}

std::string JsonValue::formatted(int indent) const
{
    return m_storage->value.dump(indent);
}

bool JsonValue::operator==(const JsonValue& other) const noexcept
{
    return m_storage->value == other.m_storage->value;
}

bool JsonValue::operator!=(const JsonValue& other) const noexcept
{
    return ! (*this == other);
}

const std::shared_ptr<detail::JsonStorage>& JsonValue::storage() const noexcept
{
    return m_storage;
}

std::shared_ptr<detail::JsonStorage>& JsonValue::storage() noexcept
{
    return m_storage;
}

}  // namespace snf
