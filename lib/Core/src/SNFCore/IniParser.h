#pragma once

/**
 * @file IniParser.h
 * @brief Simple INI parser with read/write support.
 */

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace snf {

/**
 * @class IniParser
 * @brief Parses, queries, updates, and serializes INI configuration data.
 *
 * `IniParser` supports global key/value pairs, named sections, loading from
 * disk or memory, and saving back to disk.
 */
class IniParser
{
public:
    IniParser() = default;
    ~IniParser() = default;

    bool load(const std::string& filePath);
    bool loadFromString(const std::string& content);

    bool save();
    bool save(const std::string& filePath);

    std::string toString() const;

    std::string value(const std::string& key) const;
    std::string valueOrDefault(const std::string& key, const std::string& defaultValue) const;
    std::string value(const std::string& section,
                      const std::string& key,
                      const std::string& defaultValue = "") const;

    int intValue(const std::string& key, int defaultValue = 0) const;
    int intValue(const std::string& section, const std::string& key, int defaultValue = 0) const;
    double doubleValue(const std::string& key, double defaultValue = 0.0) const;
    double doubleValue(const std::string& section, const std::string& key, double defaultValue = 0.0) const;
    bool boolValue(const std::string& key, bool defaultValue = false) const;
    bool boolValue(const std::string& section, const std::string& key, bool defaultValue = false) const;

    bool contains(const std::string& key) const;
    bool contains(const std::string& section, const std::string& key) const;
    bool hasSection(const std::string& section) const;

    std::vector<std::string> sections() const;
    std::vector<std::string> keys(const std::string& section = "") const;

    void clear();

    void setValue(const std::string& key, const std::string& value);
    void setValue(const std::string& section, const std::string& key, const std::string& value);

    bool remove(const std::string& key);
    bool remove(const std::string& section, const std::string& key);
    bool removeSection(const std::string& section);

    const std::string& errorText() const;

private:
    struct SectionData
    {
        std::map<std::string, std::string> values;
        std::vector<std::string> keyOrder;
    };

    void setError(const std::string& errorText);
    void clearError();
    SectionData* findSection(const std::string& section);
    const SectionData* findSection(const std::string& section) const;
    void ensureSectionTracked(const std::string& section);
    void ensureKeyTracked(SectionData& sectionData, const std::string& key);

private:
    std::map<std::string, SectionData> m_data;
    std::vector<std::string> m_sectionOrder;
    std::string m_filePath;
    std::string m_errorText;
};

}  // namespace snf