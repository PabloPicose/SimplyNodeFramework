#pragma once

/**
 * @file JsonArray.h
 * @brief JSON array wrapper.
 */

#include "SNFJson/JsonValue.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace snf {

/**
 * @class JsonArray
 * @brief Owns a JSON array and exposes index-based access helpers.
 */
class JsonArray
{
public:
    JsonArray();
    JsonArray(const JsonArray& other);
    JsonArray(JsonArray&& other) noexcept = default;
    JsonArray& operator=(const JsonArray& other);
    JsonArray& operator=(JsonArray&& other) noexcept = default;
    ~JsonArray() = default;

    JsonArray(std::initializer_list<JsonValue> values);

    bool empty() const noexcept;
    std::size_t size() const noexcept;

    JsonValue value(std::size_t index) const;
    JsonValue at(std::size_t index) const;

    void append(const JsonValue& value);
    bool remove(std::size_t index);
    void clear();
    std::vector<JsonValue> values() const;

    std::string toString() const;
    std::string formatted(int indent = 4) const;

private:
    friend class JsonValue;
    friend class JsonDocument;

    explicit JsonArray(std::shared_ptr<detail::JsonStorage> storage);
    const std::shared_ptr<detail::JsonStorage>& storage() const noexcept;
    std::shared_ptr<detail::JsonStorage>& storage() noexcept;

private:
    std::shared_ptr<detail::JsonStorage> m_storage;
};

}  // namespace snf
