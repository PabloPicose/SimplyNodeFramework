#pragma once

/**
 * @file CommandLineOption.h
 * @brief Command-line option descriptor for argument parsing.
 * @ingroup SNFCore_CommandLine
 */

#include <string>
#include <vector>

namespace snf {

/**
 * @class CommandLineOption
 * @ingroup SNFCore_CommandLine
 * @brief Represents a single command-line option with short and long names.
 *
 * A `CommandLineOption` describes an argument that can be passed to the program.
 * It can be a flag (boolean, no value) or an option that takes a value.
 *
 * Typical usage:
 * @code
 * CommandLineOption helpOption({"h", "help"}, "Show help message");
 * CommandLineOption inputOption({"i", "input"}, "Input file", "input.txt");
 * @endcode
 */
class CommandLineOption
{
public:
    /**
     * @brief Constructs a flag option (no value expected).
     * @param names A list of option names. Typically ["h", "help"] or ["v", "version"].
     * @param description Human-readable description of what this option does.
     */
    CommandLineOption(const std::vector<std::string>& names, const std::string& description);

    /**
     * @brief Constructs an option that takes a value.
     * @param names A list of option names.
     * @param description Human-readable description.
     * @param defaultValue Default value if the option is not provided.
     */
    CommandLineOption(const std::vector<std::string>& names,
                      const std::string& description,
                      const std::string& defaultValue);

    /**
     * @brief Returns the primary (first) option name without leading dashes.
     *
     * Useful for identifying the option in maps or logs.
     */
    std::string name() const;

    /**
     * @brief Returns all registered names for this option.
     *
     * Typically includes both short (e.g., "h") and long (e.g., "help") forms.
     */
    const std::vector<std::string>& names() const;

    /** @brief Returns the human-readable description of this option. */
    const std::string& description() const;

    /**
     * @brief Returns the default value.
     *
     * For flags, this is typically "false" or empty. For options with values,
     * this is the default if not provided on the command line.
     */
    const std::string& defaultValue() const;

    /**
     * @brief Returns the currently set value.
     *
     * If the option was not provided and has no default, returns an empty string.
     */
    const std::string& value() const;

    /**
     * @brief Returns true if this is a flag (no value expected).
     *
     * Flags are options that do not take an argument value.
     */
    bool isFlag() const;

    /**
     * @brief Returns true if this option was explicitly provided on the command line.
     */
    bool isSet() const;

private:
    friend class CommandLineParser;

    // Internal setters used by CommandLineParser
    void setValue(const std::string& value);
    void setIsSet(bool set);

private:
    std::vector<std::string> m_names;
    std::string m_description;
    std::string m_defaultValue;
    std::string m_value;
    bool m_isFlag = true;
    bool m_isSet = false;
};

}  // namespace snf
