#pragma once

/**
 * @file JsonValue.h
 * @brief Value wrapper around nlohmann::json.
 */

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

namespace snf {

class JsonArray;
class JsonDocument;
class JsonObject;

namespace detail {
class JsonStorage;
}

/**
 * @class JsonValue
 * @brief Owns any JSON value: null, bool, number, string, object, or array.
 */
class JsonValue
{
public:
    enum class Type
    {
        Null,
        Boolean,
        NumberInteger,
        NumberUnsigned,
        NumberFloat,
        String,
        Object,
        Array
    };

    JsonValue();
    JsonValue(const JsonValue& other);
    JsonValue(JsonValue&& other) noexcept = default;
    JsonValue& operator=(const JsonValue& other);
    JsonValue& operator=(JsonValue&& other) noexcept = default;
    ~JsonValue() = default;

    JsonValue(std::nullptr_t);
    JsonValue(bool value);
    JsonValue(int value);
    JsonValue(long long value);
    JsonValue(unsigned int value);
    JsonValue(unsigned long long value);
    JsonValue(double value);
    JsonValue(const char* value);
    JsonValue(const std::string& value);
    JsonValue(std::string_view value);
    JsonValue(const JsonObject& value);
    JsonValue(const JsonArray& value);

    static JsonValue object();
    static JsonValue array();

    Type type() const noexcept;
    bool isNull() const noexcept;
    bool isBool() const noexcept;
    bool isNumber() const noexcept;
    bool isInteger() const noexcept;
    bool isUnsigned() const noexcept;
    bool isFloat() const noexcept;
    bool isString() const noexcept;
    bool isObject() const noexcept;
    bool isArray() const noexcept;

    bool asBool() const;
    int asInt() const;
    long long asInt64() const;
    unsigned long long asUInt64() const;
    double asDouble() const;
    std::string asString() const;
    JsonObject asObject() const;
    JsonArray asArray() const;

    std::string toString() const;
    std::string formatted(int indent = 4) const;

    bool operator==(const JsonValue& other) const noexcept;
    bool operator!=(const JsonValue& other) const noexcept;

private:
    friend class JsonArray;
    friend class JsonDocument;
    friend class JsonObject;

    explicit JsonValue(std::shared_ptr<detail::JsonStorage> storage);
    const std::shared_ptr<detail::JsonStorage>& storage() const noexcept;
    std::shared_ptr<detail::JsonStorage>& storage() noexcept;

private:
    std::shared_ptr<detail::JsonStorage> m_storage;
};

}  // namespace snf
