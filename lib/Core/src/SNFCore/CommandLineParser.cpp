#include "SNFCore/CommandLineParser.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace snf {

CommandLineParser::CommandLineParser() = default;

void CommandLineParser::addOption(const CommandLineOption& option)
{
    // Remove existing option with same primary name if it exists
    auto it = std::find_if(m_options.begin(), m_options.end(), [&option](const CommandLineOption& opt) {
        return opt.name() == option.name();
    });

    if (it != m_options.end()) {
        *it = option;
    } else {
        m_options.push_back(option);
    }
}

void CommandLineParser::setApplicationVersion(const std::string& version) { m_version = version; }

const std::string& CommandLineParser::applicationVersion() const { return m_version; }

bool CommandLineParser::parse(int argc, char** argv)
{
    m_errorText.clear();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // Check if user has registered "help" or "version" options
        auto* helpOpt = findOption("help");
        auto* versionOpt = findOption("version");

        // Handle --help or -h: built-in if not registered, user option if registered
        if ((arg == "--help" || arg == "-h") && !helpOpt) {
            printHelp();
            std::exit(0);
        }

        // Handle --version: built-in if not registered and version is set, user option if registered
        if ((arg == "--version") && !versionOpt && !m_version.empty()) {
            std::cout << m_version << std::endl;
            std::exit(0);
        }

        // Handle long option: --name or --name=value
        if (arg.substr(0, 2) == "--") {
            size_t eqPos = arg.find('=');
            std::string optName;
            std::string optValue;

            if (eqPos != std::string::npos) {
                optName = arg.substr(2, eqPos - 2);
                optValue = arg.substr(eqPos + 1);
            } else {
                optName = arg.substr(2);
                optValue.clear();
            }

            auto* opt = findOption(optName);
            if (!opt) {
                m_errorText = "Unknown option: --" + optName;
                return false;
            }

            if (opt->isFlag()) {
                if (!optValue.empty()) {
                    m_errorText = "Option --" + optName + " does not take a value";
                    return false;
                }
                opt->setIsSet(true);
            } else {
                if (optValue.empty()) {
                    if (i + 1 >= argc) {
                        m_errorText = "Option --" + optName + " requires a value";
                        return false;
                    }
                    optValue = argv[++i];
                }
                opt->setValue(optValue);
            }
            continue;
        }

        // Handle short option(s): -a, -b, -o value, -o=value
        if (arg.substr(0, 1) == "-" && arg.length() > 1) {
            std::string shortNames = arg.substr(1);

            // Check for -name=value format
            size_t eqPos = shortNames.find('=');
            if (eqPos != std::string::npos) {
                std::string optName = shortNames.substr(0, eqPos);
                std::string optValue = shortNames.substr(eqPos + 1);

                auto* opt = findOption(optName);
                if (!opt) {
                    m_errorText = "Unknown option: -" + optName;
                    return false;
                }

                if (opt->isFlag()) {
                    m_errorText = "Option -" + optName + " does not take a value";
                    return false;
                }
                opt->setValue(optValue);
                continue;
            }

            // Handle -abc (multiple short flags) or -o value
            if (shortNames.length() == 1) {
                // Single short option: -o
                auto* opt = findOption(shortNames);
                if (!opt) {
                    m_errorText = "Unknown option: -" + shortNames;
                    return false;
                }

                if (opt->isFlag()) {
                    opt->setIsSet(true);
                } else {
                    if (i + 1 >= argc) {
                        m_errorText = "Option -" + shortNames + " requires a value";
                        return false;
                    }
                    opt->setValue(argv[++i]);
                }
            } else {
                // Multiple short flags: -abc treated as -a -b -c (all must be flags)
                for (char c : shortNames) {
                    std::string singleName(1, c);
                    auto* opt = findOption(singleName);
                    if (!opt) {
                        m_errorText = "Unknown option: -" + singleName;
                        return false;
                    }
                    if (!opt->isFlag()) {
                        m_errorText = "Option -" + singleName + " requires a value and cannot be combined";
                        return false;
                    }
                    opt->setIsSet(true);
                }
            }
            continue;
        }

        // Positional argument or unknown format
        m_errorText = "Unknown argument: " + arg;
        return false;
    }

    return true;
}

std::string CommandLineParser::value(const std::string& name) const
{
    const auto* opt = findOption(name);
    if (!opt) {
        return std::string();
    }
    if (opt->isSet()) {
        return opt->value();
    }
    return opt->defaultValue();
}

bool CommandLineParser::isSet(const std::string& name) const
{
    const auto* opt = findOption(name);
    return opt && opt->isSet();
}

void CommandLineParser::showHelp() const { printHelp(); }

const std::string& CommandLineParser::errorText() const { return m_errorText; }

CommandLineOption* CommandLineParser::findOption(const std::string& name)
{
    for (auto& opt : m_options) {
        for (const auto& optName : opt.names()) {
            if (optName == name) {
                return &opt;
            }
        }
    }
    return nullptr;
}

const CommandLineOption* CommandLineParser::findOption(const std::string& name) const
{
    for (const auto& opt : m_options) {
        for (const auto& optName : opt.names()) {
            if (optName == name) {
                return &opt;
            }
        }
    }
    return nullptr;
}

void CommandLineParser::printHelp() const
{
    std::cout << "Usage: [options]\n\n";

    if (!m_version.empty()) {
        std::cout << "Version: " << m_version << "\n\n";
    }

    std::cout << "Options:\n";

    // Calculate column widths for nice formatting
    size_t maxNamesWidth = 0;
    for (const auto& opt : m_options) {
        std::string names;
        for (size_t i = 0; i < opt.names().size(); ++i) {
            if (i > 0) {
                names += ", ";
            }
            if (opt.names()[i].length() == 1) {
                names += "-" + opt.names()[i];
            } else {
                names += "--" + opt.names()[i];
            }
        }
        maxNamesWidth = std::max(maxNamesWidth, names.length());
    }

    // Add space for default value labels
    maxNamesWidth = std::max(maxNamesWidth, static_cast<size_t>(20));

    for (const auto& opt : m_options) {
        std::string names;
        for (size_t i = 0; i < opt.names().size(); ++i) {
            if (i > 0) {
                names += ", ";
            }
            if (opt.names()[i].length() == 1) {
                names += "-" + opt.names()[i];
            } else {
                names += "--" + opt.names()[i];
            }
        }

        std::cout << "  " << std::left << names;

        // Padding between names and description
        for (size_t i = names.length(); i < maxNamesWidth + 2; ++i) {
            std::cout << " ";
        }

        std::cout << opt.description();

        if (!opt.defaultValue().empty() && !opt.isFlag()) {
            std::cout << " (default: " << opt.defaultValue() << ")";
        }

        std::cout << "\n";
    }

    std::cout << "  -h, --help" << std::string(maxNamesWidth - 10 + 2, ' ') << "Show this help message\n";

    if (!m_version.empty()) {
        std::cout << "  --version" << std::string(maxNamesWidth - 9 + 2, ' ') << "Show version\n";
    }
}

}  // namespace snf
