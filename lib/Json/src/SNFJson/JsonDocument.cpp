#include "SNFJson/JsonDocument.h"

#include "SNFJson/JsonStorage.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace snf {

namespace {

struct SourcePosition
{
    std::size_t line = 1;
    std::size_t column = 1;
};

SourcePosition sourcePositionForByte(std::string_view content, std::size_t byte)
{
    if (byte == 0) {
        return SourcePosition{};
    }

    const std::size_t offset = std::min(byte - 1, content.size());
    SourcePosition position;

    for (std::size_t i = 0; i < offset; ++i) {
        if (content[i] == '\n') {
            ++position.line;
            position.column = 1;
        } else {
            ++position.column;
        }
    }

    return position;
}

JsonParseError makeParseError(const nlohmann::json::parse_error& error, std::string_view content)
{
    const SourcePosition position = sourcePositionForByte(content, error.byte);
    std::ostringstream message;
    message << "JSON parse error at line " << position.line << ", column " << position.column << ": "
            << error.what();
    return JsonParseError(message.str(), error.byte, position.line, position.column);
}

std::string readFile(const std::string& filePath)
{
    std::ifstream input(filePath);
    if (! input.is_open()) {
        throw std::runtime_error("Failed to open JSON file: " + filePath);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

JsonDocument::JsonDocument() = default;

JsonDocument::JsonDocument(const JsonValue& root)
    : m_root(root)
{
}

JsonDocument::JsonDocument(const JsonObject& root)
    : m_root(root)
{
}

JsonDocument::JsonDocument(const JsonArray& root)
    : m_root(root)
{
}

JsonDocument JsonDocument::parse(std::string_view content)
{
    try {
        return JsonDocument(JsonValue(detail::makeJsonStorage(nlohmann::json::parse(content.begin(), content.end()))));
    } catch (const nlohmann::json::parse_error& error) {
        throw makeParseError(error, content);
    }
}

JsonDocument JsonDocument::fromFile(const std::string& filePath)
{
    return parse(readFile(filePath));
}

bool JsonDocument::loadFromString(std::string_view content)
{
    try {
        *this = parse(content);
        m_errorText.clear();
        return true;
    } catch (const std::exception& error) {
        m_errorText = error.what();
        return false;
    }
}

bool JsonDocument::load(const std::string& filePath)
{
    try {
        return loadFromString(readFile(filePath));
    } catch (const std::exception& error) {
        m_errorText = error.what();
        return false;
    }
}

bool JsonDocument::save(const std::string& filePath, int indent) const
{
    std::ofstream output(filePath, std::ios::trunc);
    if (! output.is_open()) {
        return false;
    }

    output << formatted(indent);
    output.flush();
    return output.good();
}

const JsonValue& JsonDocument::root() const noexcept
{
    return m_root;
}

JsonValue& JsonDocument::root() noexcept
{
    return m_root;
}

void JsonDocument::setRoot(const JsonValue& root)
{
    m_root = root;
    m_errorText.clear();
}

bool JsonDocument::isObject() const noexcept
{
    return m_root.isObject();
}

bool JsonDocument::isArray() const noexcept
{
    return m_root.isArray();
}

JsonObject JsonDocument::object() const
{
    return m_root.asObject();
}

JsonArray JsonDocument::array() const
{
    return m_root.asArray();
}

std::string JsonDocument::toString() const
{
    return m_root.toString();
}

std::string JsonDocument::formatted(int indent) const
{
    return m_root.formatted(indent);
}

const std::string& JsonDocument::errorText() const noexcept
{
    return m_errorText;
}

}  // namespace snf
