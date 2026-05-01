#include "SNFJson/JsonArray.h"

#include "SNFJson/JsonStorage.h"

#include <stdexcept>
#include <utility>

namespace snf {

JsonArray::JsonArray()
    : m_storage(detail::makeJsonStorage(nlohmann::json::array()))
{
}

JsonArray::JsonArray(const JsonArray& other)
    : m_storage(detail::makeJsonStorage(other.m_storage->value))
{
}

JsonArray& JsonArray::operator=(const JsonArray& other)
{
    if (this != &other) {
        m_storage = detail::makeJsonStorage(other.m_storage->value);
    }

    return *this;
}

JsonArray::JsonArray(std::initializer_list<JsonValue> values)
    : JsonArray()
{
    for (const auto& value : values) {
        append(value);
    }
}

JsonArray::JsonArray(std::shared_ptr<detail::JsonStorage> storage)
    : m_storage(std::move(storage))
{
    if (m_storage->value.is_null()) {
        m_storage->value = nlohmann::json::array();
    }

    if (! m_storage->value.is_array()) {
        throw std::runtime_error("JSON value is not an array");
    }
}

bool JsonArray::empty() const noexcept
{
    return m_storage->value.empty();
}

std::size_t JsonArray::size() const noexcept
{
    return m_storage->value.size();
}

JsonValue JsonArray::value(std::size_t index) const
{
    if (index >= m_storage->value.size()) {
        return JsonValue();
    }

    return JsonValue(detail::makeJsonStorage(m_storage->value[index]));
}

JsonValue JsonArray::at(std::size_t index) const
{
    if (index >= m_storage->value.size()) {
        throw std::out_of_range("JSON array index out of range");
    }

    return JsonValue(detail::makeJsonStorage(m_storage->value[index]));
}

void JsonArray::append(const JsonValue& value)
{
    m_storage->value.push_back(value.storage()->value);
}

bool JsonArray::remove(std::size_t index)
{
    if (index >= m_storage->value.size()) {
        return false;
    }

    m_storage->value.erase(m_storage->value.begin() + static_cast<nlohmann::json::difference_type>(index));
    return true;
}

void JsonArray::clear()
{
    m_storage->value.clear();
}

std::vector<JsonValue> JsonArray::values() const
{
    std::vector<JsonValue> result;
    result.reserve(m_storage->value.size());

    for (const auto& value : m_storage->value) {
        result.push_back(JsonValue(detail::makeJsonStorage(value)));
    }

    return result;
}

std::string JsonArray::toString() const
{
    return m_storage->value.dump();
}

std::string JsonArray::formatted(int indent) const
{
    return m_storage->value.dump(indent);
}

const std::shared_ptr<detail::JsonStorage>& JsonArray::storage() const noexcept
{
    return m_storage;
}

std::shared_ptr<detail::JsonStorage>& JsonArray::storage() noexcept
{
    return m_storage;
}

}  // namespace snf
