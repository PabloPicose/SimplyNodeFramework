#include "SNFCore/IniParser.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace snf {

namespace {

std::string trim(const std::string& value)
{
    std::string::size_type begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::string::size_type end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

bool isCommentLine(const std::string& line)
{
    return ! line.empty() && (line.front() == ';' || line.front() == '#');
}

std::string stripInlineComment(const std::string& value)
{
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char current = value[i];
        if ((current == ';' || current == '#') && (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1])))) {
            return trim(value.substr(0, i));
        }
    }

    return trim(value);
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::optional<int> parseInt(const std::string& value)
{
    char* end = nullptr;
    errno = 0;
    const long parsedValue = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || errno == ERANGE ||
        parsedValue < std::numeric_limits<int>::min() || parsedValue > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    return static_cast<int>(parsedValue);
}

std::optional<double> parseDouble(const std::string& value)
{
    char* end = nullptr;
    errno = 0;
    const double parsedValue = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || errno == ERANGE) {
        return std::nullopt;
    }

    return parsedValue;
}

std::optional<bool> parseBool(const std::string& value)
{
    const std::string lowered = toLower(trim(value));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }

    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }

    return std::nullopt;
}

}  // namespace

bool IniParser::load(const std::string& filePath)
{
    std::ifstream input(filePath);
    if (! input.is_open()) {
        clear();
        m_filePath.clear();
        setError("Failed to open INI file: " + filePath);
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (! loadFromString(buffer.str())) {
        m_filePath.clear();
        return false;
    }

    m_filePath = filePath;
    return true;
}

bool IniParser::loadFromString(const std::string& content)
{
    std::map<std::string, SectionData> parsedData;
    std::vector<std::string> parsedSectionOrder;
    std::string currentSection;

    std::istringstream input(content);
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;

        if (! line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmedLine = trim(line);
        if (trimmedLine.empty() || isCommentLine(trimmedLine)) {
            continue;
        }

        if (trimmedLine.front() == '[') {
            if (trimmedLine.back() != ']') {
                setError("Invalid section header on line " + std::to_string(lineNumber));
                return false;
            }

            currentSection = trim(trimmedLine.substr(1, trimmedLine.size() - 2));
            if (currentSection.empty()) {
                setError("Empty section name on line " + std::to_string(lineNumber));
                return false;
            }

            if (parsedData.find(currentSection) == parsedData.end()) {
                parsedData[currentSection] = SectionData{};
                parsedSectionOrder.push_back(currentSection);
            }
            continue;
        }

        const std::string::size_type equalsPosition = trimmedLine.find('=');
        if (equalsPosition == std::string::npos) {
            setError("Invalid key/value entry on line " + std::to_string(lineNumber));
            return false;
        }

        const std::string key = trim(trimmedLine.substr(0, equalsPosition));
        const std::string value = stripInlineComment(trimmedLine.substr(equalsPosition + 1));
        if (key.empty()) {
            setError("Empty key on line " + std::to_string(lineNumber));
            return false;
        }

        SectionData& sectionData = parsedData[currentSection];
        if (sectionData.values.find(key) == sectionData.values.end()) {
            sectionData.keyOrder.push_back(key);
        }
        sectionData.values[key] = value;
        if (! currentSection.empty() &&
            std::find(parsedSectionOrder.begin(), parsedSectionOrder.end(), currentSection) == parsedSectionOrder.end()) {
            parsedSectionOrder.push_back(currentSection);
        }
    }

    m_data = std::move(parsedData);
    m_sectionOrder = std::move(parsedSectionOrder);
    m_filePath.clear();
    clearError();
    return true;
}

bool IniParser::save()
{
    if (m_filePath.empty()) {
        setError("No INI file path available for save()");
        return false;
    }

    return save(m_filePath);
}

bool IniParser::save(const std::string& filePath)
{
    std::ofstream output(filePath, std::ios::trunc);
    if (! output.is_open()) {
        setError("Failed to write INI file: " + filePath);
        return false;
    }

    output << toString();
    output.flush();
    if (! output.good()) {
        setError("Failed while writing INI file: " + filePath);
        return false;
    }

    m_filePath = filePath;
    clearError();
    return true;
}

std::string IniParser::toString() const
{
    std::ostringstream output;

    const SectionData* globalSection = findSection("");
    if (globalSection != nullptr) {
        for (const auto& key : globalSection->keyOrder) {
            auto valueIt = globalSection->values.find(key);
            if (valueIt != globalSection->values.end()) {
                output << key << " = " << valueIt->second << '\n';
            }
        }
    }

    bool wroteSection = globalSection != nullptr && ! globalSection->keyOrder.empty();
    for (const auto& section : m_sectionOrder) {
        const SectionData* sectionData = findSection(section);
        if (section.empty() || sectionData == nullptr) {
            continue;
        }

        if (wroteSection) {
            output << '\n';
        }

        output << '[' << section << "]\n";
        for (const auto& key : sectionData->keyOrder) {
            auto valueIt = sectionData->values.find(key);
            if (valueIt != sectionData->values.end()) {
                output << key << " = " << valueIt->second << '\n';
            }
        }
        wroteSection = true;
    }

    return output.str();
}

std::string IniParser::value(const std::string& key) const
{
    return value("", key, "");
}

std::string IniParser::valueOrDefault(const std::string& key, const std::string& defaultValue) const
{
    return value("", key, defaultValue);
}

std::string IniParser::value(const std::string& section,
                            const std::string& key,
                            const std::string& defaultValue) const
{
    const SectionData* sectionData = findSection(section);
    if (sectionData == nullptr) {
        return defaultValue;
    }

    auto it = sectionData->values.find(key);
    if (it == sectionData->values.end()) {
        return defaultValue;
    }

    return it->second;
}

int IniParser::intValue(const std::string& key, int defaultValue) const
{
    return intValue("", key, defaultValue);
}

int IniParser::intValue(const std::string& section, const std::string& key, int defaultValue) const
{
    const auto parsedValue = parseInt(value(section, key));
    return parsedValue.has_value() ? *parsedValue : defaultValue;
}

double IniParser::doubleValue(const std::string& key, double defaultValue) const
{
    return doubleValue("", key, defaultValue);
}

double IniParser::doubleValue(const std::string& section, const std::string& key, double defaultValue) const
{
    const auto parsedValue = parseDouble(value(section, key));
    return parsedValue.has_value() ? *parsedValue : defaultValue;
}

bool IniParser::boolValue(const std::string& key, bool defaultValue) const
{
    return boolValue("", key, defaultValue);
}

bool IniParser::boolValue(const std::string& section, const std::string& key, bool defaultValue) const
{
    const auto parsedValue = parseBool(value(section, key));
    return parsedValue.has_value() ? *parsedValue : defaultValue;
}

bool IniParser::contains(const std::string& key) const { return contains("", key); }

bool IniParser::contains(const std::string& section, const std::string& key) const
{
    const SectionData* sectionData = findSection(section);
    return sectionData != nullptr && sectionData->values.find(key) != sectionData->values.end();
}

bool IniParser::hasSection(const std::string& section) const
{
    return ! section.empty() && m_data.find(section) != m_data.end();
}

std::vector<std::string> IniParser::sections() const { return m_sectionOrder; }

std::vector<std::string> IniParser::keys(const std::string& section) const
{
    std::vector<std::string> result;

    const SectionData* sectionData = findSection(section);
    if (sectionData == nullptr) {
        return result;
    }

    result = sectionData->keyOrder;
    return result;
}

void IniParser::clear()
{
    m_data.clear();
    m_sectionOrder.clear();
    clearError();
}

void IniParser::setValue(const std::string& key, const std::string& value) { setValue("", key, value); }

void IniParser::setValue(const std::string& section, const std::string& key, const std::string& value)
{
    SectionData& sectionData = m_data[section];
    ensureKeyTracked(sectionData, key);
    sectionData.values[key] = value;
    ensureSectionTracked(section);
    clearError();
}

bool IniParser::remove(const std::string& key) { return remove("", key); }

bool IniParser::remove(const std::string& section, const std::string& key)
{
    SectionData* sectionData = findSection(section);
    if (sectionData == nullptr) {
        return false;
    }

    auto it = sectionData->values.find(key);
    if (it == sectionData->values.end()) {
        return false;
    }

    sectionData->values.erase(it);
    sectionData->keyOrder.erase(
        std::remove(sectionData->keyOrder.begin(), sectionData->keyOrder.end(), key), sectionData->keyOrder.end());
    if (! section.empty() && sectionData->values.empty()) {
        m_data.erase(section);
        m_sectionOrder.erase(std::remove(m_sectionOrder.begin(), m_sectionOrder.end(), section), m_sectionOrder.end());
    }

    return true;
}

bool IniParser::removeSection(const std::string& section)
{
    if (section.empty()) {
        return false;
    }

    auto it = m_data.find(section);
    if (it == m_data.end()) {
        return false;
    }

    m_data.erase(it);
    m_sectionOrder.erase(std::remove(m_sectionOrder.begin(), m_sectionOrder.end(), section), m_sectionOrder.end());
    return true;
}

const std::string& IniParser::errorText() const { return m_errorText; }

void IniParser::setError(const std::string& errorText) { m_errorText = errorText; }

void IniParser::clearError() { m_errorText.clear(); }

IniParser::SectionData* IniParser::findSection(const std::string& section)
{
    auto it = m_data.find(section);
    if (it == m_data.end()) {
        return nullptr;
    }

    return &it->second;
}

const IniParser::SectionData* IniParser::findSection(const std::string& section) const
{
    auto it = m_data.find(section);
    if (it == m_data.end()) {
        return nullptr;
    }

    return &it->second;
}

void IniParser::ensureSectionTracked(const std::string& section)
{
    if (section.empty()) {
        return;
    }

    if (std::find(m_sectionOrder.begin(), m_sectionOrder.end(), section) == m_sectionOrder.end()) {
        m_sectionOrder.push_back(section);
    }
}

void IniParser::ensureKeyTracked(SectionData& sectionData, const std::string& key)
{
    if (std::find(sectionData.keyOrder.begin(), sectionData.keyOrder.end(), key) == sectionData.keyOrder.end()) {
        sectionData.keyOrder.push_back(key);
    }
}

}  // namespace snf