#include "SNFJson/JsonObject.h"

#include "SNFJson/JsonStorage.h"

#include <stdexcept>
#include <utility>

namespace snf {

JsonObject::JsonObject()
    : m_storage(detail::makeJsonStorage(nlohmann::json::object()))
{
}

JsonObject::JsonObject(const JsonObject& other)
    : m_storage(detail::makeJsonStorage(other.m_storage->value))
{
}

JsonObject& JsonObject::operator=(const JsonObject& other)
{
    if (this != &other) {
        m_storage = detail::makeJsonStorage(other.m_storage->value);
    }

    return *this;
}

JsonObject::JsonObject(std::initializer_list<std::pair<std::string, JsonValue>> values)
    : JsonObject()
{
    for (const auto& entry : values) {
        set(entry.first, entry.second);
    }
}

JsonObject::JsonObject(std::shared_ptr<detail::JsonStorage> storage)
    : m_storage(std::move(storage))
{
    if (m_storage->value.is_null()) {
        m_storage->value = nlohmann::json::object();
    }

    if (! m_storage->value.is_object()) {
        throw std::runtime_error("JSON value is not an object");
    }
}

bool JsonObject::empty() const noexcept
{
    return m_storage->value.empty();
}

std::size_t JsonObject::size() const noexcept
{
    return m_storage->value.size();
}

bool JsonObject::contains(const std::string& key) const
{
    return m_storage->value.contains(key);
}

std::vector<std::string> JsonObject::keys() const
{
    std::vector<std::string> result;
    result.reserve(m_storage->value.size());

    for (auto it = m_storage->value.begin(); it != m_storage->value.end(); ++it) {
        result.push_back(it.key());
    }

    return result;
}

JsonValue JsonObject::value(const std::string& key) const
{
    const auto it = m_storage->value.find(key);
    if (it == m_storage->value.end()) {
        return JsonValue();
    }

    return JsonValue(detail::makeJsonStorage(*it));
}

JsonValue JsonObject::valueOr(const std::string& key, const JsonValue& fallback) const
{
    const auto it = m_storage->value.find(key);
    if (it == m_storage->value.end()) {
        return fallback;
    }

    return JsonValue(detail::makeJsonStorage(*it));
}

JsonValue JsonObject::operator[](const std::string& key) const
{
    return value(key);
}

void JsonObject::set(const std::string& key, const JsonValue& value)
{
    m_storage->value[key] = value.storage()->value;
}

bool JsonObject::remove(const std::string& key)
{
    return m_storage->value.erase(key) > 0;
}

void JsonObject::clear()
{
    m_storage->value.clear();
}

std::string JsonObject::toString() const
{
    return m_storage->value.dump();
}

std::string JsonObject::formatted(int indent) const
{
    return m_storage->value.dump(indent);
}

const std::shared_ptr<detail::JsonStorage>& JsonObject::storage() const noexcept
{
    return m_storage;
}

std::shared_ptr<detail::JsonStorage>& JsonObject::storage() noexcept
{
    return m_storage;
}

}  // namespace snf
