#pragma once

/**
 * @file JsonObject.h
 * @brief JSON object wrapper.
 */

#include "SNFJson/JsonValue.h"

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace snf {

/**
 * @class JsonObject
 * @brief Owns a JSON object and exposes key-based access helpers.
 */
class JsonObject
{
public:
    JsonObject();
    JsonObject(const JsonObject& other);
    JsonObject(JsonObject&& other) noexcept = default;
    JsonObject& operator=(const JsonObject& other);
    JsonObject& operator=(JsonObject&& other) noexcept = default;
    ~JsonObject() = default;

    JsonObject(std::initializer_list<std::pair<std::string, JsonValue>> values);

    bool empty() const noexcept;
    std::size_t size() const noexcept;
    bool contains(const std::string& key) const;
    std::vector<std::string> keys() const;

    JsonValue value(const std::string& key) const;
    JsonValue valueOr(const std::string& key, const JsonValue& fallback) const;
    JsonValue operator[](const std::string& key) const;

    void set(const std::string& key, const JsonValue& value);
    bool remove(const std::string& key);
    void clear();

    std::string toString() const;
    std::string formatted(int indent = 4) const;

private:
    friend class JsonValue;
    friend class JsonDocument;

    explicit JsonObject(std::shared_ptr<detail::JsonStorage> storage);
    const std::shared_ptr<detail::JsonStorage>& storage() const noexcept;
    std::shared_ptr<detail::JsonStorage>& storage() noexcept;

private:
    std::shared_ptr<detail::JsonStorage> m_storage;
};

}  // namespace snf
