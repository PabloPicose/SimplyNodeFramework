#pragma once

/**
 * @file CommandLineParser.h
 * @brief Command-line argument parser with help generation.
 * @ingroup SNFCore_CommandLine
 */

#include <SNFCore/CommandLineOption.h>

#include <memory>
#include <string>
#include <vector>

namespace snf {

/**
 * @class CommandLineParser
 * @ingroup SNFCore_CommandLine
 * @brief Parses command-line arguments into a collection of registered options.
 *
 * `CommandLineParser` provides command-line argument parsing with
 * automatic help generation. Register options via `addOption()`, then call
 * `parse()` with argc/argv. Retrieve parsed values with `value()` or
 * `isSet()`.
 *
 * Supported formats:
 * - Short flags: `-h`, `-v`
 * - Long flags: `--help`, `--version`
 * - Options with values: `-o value`, `-o=value`, `--option value`, `--option=value`
 * - Built-in: `--help` and `--version` (if version is set)
 *
 * Typical usage:
 * @code
 * CommandLineParser parser;
 * parser.setApplicationVersion("1.0.0");
 * parser.addOption(CommandLineOption({"h", "help"}, "Show help"));
 * parser.addOption(CommandLineOption({"i", "input"}, "Input file", "input.txt"));
 *
 * if (!parser.parse(argc, argv)) {
 *     std::cerr << parser.errorText() << std::endl;
 *     parser.showHelp();
 *     return 1;
 * }
 *
 * if (parser.isSet("help")) {
 *     parser.showHelp();
 *     return 0;
 * }
 *
 * std::string inputFile = parser.value("input");
 * @endcode
 *
 * @sa CommandLineOption
 */
class CommandLineParser
{
public:
    /**
     * @brief Constructs an empty parser.
     *
     * The parser is not yet associated with any options. Call `addOption()`
     * to register options before calling `parse()`.
     */
    CommandLineParser();

    ~CommandLineParser() = default;

    /**
     * @brief Adds an option to the parser.
     *
     * Options should be added before calling `parse()`. If the same option
     * name is added twice, the second definition overwrites the first.
     *
     * @param option The CommandLineOption to register.
     */
    void addOption(const CommandLineOption& option);

    /**
     * @brief Sets the application version string.
     *
     * This enables the built-in `--version` flag, which prints the version
     * and exits (calls `std::exit(0)`).
     *
     * @param version Human-readable version string, e.g., "1.0.0".
     */
    void setApplicationVersion(const std::string& version);

    /**
     * @brief Returns the currently set application version.
     */
    const std::string& applicationVersion() const;

    /**
     * @brief Parses command-line arguments.
     *
     * Processes @p argc and @p argv, matching them against registered options.
     * Unrecognized options are reported in `errorText()`.
     *
     * Built-in options:
     * - `--help` (or `-h` if registered): Shows help and calls `std::exit(0)`.
     * - `--version`: Shows version and calls `std::exit(0)`.
     *
     * @param argc Argument count (including program name).
     * @param argv Argument vector.
     * @return True if parsing succeeded, false if an error occurred.
     */
    bool parse(int argc, char** argv);

    /**
     * @brief Returns the value of the option identified by @p name.
     *
     * For flags, returns "true" if set, otherwise the default or empty string.
     * For options, returns the provided value or the default if not set.
     *
     * @param name The option name (short form, e.g., "h") or long form
     *             without leading dashes.
     * @return The option value, or empty string if not found.
     */
    std::string value(const std::string& name) const;

    /**
     * @brief Returns true if the option identified by @p name was explicitly
     *        provided on the command line.
     *
     * @param name The option name.
     * @return True if set, false otherwise.
     */
    bool isSet(const std::string& name) const;

    /**
     * @brief Displays formatted help to stdout.
     *
     * Shows usage line, description (if set), and a table of all options
     * with their short/long names, descriptions, and default values.
     */
    void showHelp() const;

    /**
     * @brief Returns a human-readable error message if parsing failed.
     *
     * Empty string if parsing succeeded.
     */
    const std::string& errorText() const;

private:
    CommandLineOption* findOption(const std::string& name);
    const CommandLineOption* findOption(const std::string& name) const;

    void printHelp() const;

private:
    std::vector<CommandLineOption> m_options;
    std::string m_version;
    std::string m_errorText;
};

}  // namespace snf
