#pragma once

/**
 * @file JsonDocument.h
 * @brief JSON document parsing, loading, saving, and formatting.
 */

#include "SNFJson/JsonArray.h"
#include "SNFJson/JsonObject.h"
#include "SNFJson/JsonParseError.h"
#include "SNFJson/JsonValue.h"

#include <string>
#include <string_view>

namespace snf {

/**
 * @class JsonDocument
 * @brief Owns a root JSON value and provides document-level I/O helpers.
 */
class JsonDocument
{
public:
    JsonDocument();
    explicit JsonDocument(const JsonValue& root);
    explicit JsonDocument(const JsonObject& root);
    explicit JsonDocument(const JsonArray& root);

    static JsonDocument parse(std::string_view content);
    static JsonDocument fromFile(const std::string& filePath);

    bool loadFromString(std::string_view content);
    bool load(const std::string& filePath);
    bool save(const std::string& filePath, int indent = 4) const;

    const JsonValue& root() const noexcept;
    JsonValue& root() noexcept;
    void setRoot(const JsonValue& root);

    bool isObject() const noexcept;
    bool isArray() const noexcept;
    JsonObject object() const;
    JsonArray array() const;

    std::string toString() const;
    std::string formatted(int indent = 4) const;
    const std::string& errorText() const noexcept;

private:
    JsonValue m_root;
    std::string m_errorText;
};

}  // namespace snf
