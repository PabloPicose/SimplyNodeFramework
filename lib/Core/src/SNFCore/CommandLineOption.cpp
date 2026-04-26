#include "SNFCore/CommandLineOption.h"

namespace snf {

CommandLineOption::CommandLineOption(const std::vector<std::string>& names,
                                     const std::string& description)
    : m_names(names), m_description(description), m_isFlag(true), m_isSet(false)
{
}

CommandLineOption::CommandLineOption(const std::vector<std::string>& names,
                                     const std::string& description,
                                     const std::string& defaultValue)
    : m_names(names), m_description(description), m_defaultValue(defaultValue), m_value(defaultValue), m_isFlag(false), m_isSet(false)
{
}

std::string CommandLineOption::name() const
{
    return m_names.empty() ? std::string() : m_names[0];
}

const std::vector<std::string>& CommandLineOption::names() const { return m_names; }

const std::string& CommandLineOption::description() const { return m_description; }

const std::string& CommandLineOption::defaultValue() const { return m_defaultValue; }

const std::string& CommandLineOption::value() const { return m_value; }

bool CommandLineOption::isFlag() const { return m_isFlag; }

bool CommandLineOption::isSet() const { return m_isSet; }

void CommandLineOption::setValue(const std::string& value)
{
    m_value = value;
    m_isSet = true;
}

void CommandLineOption::setIsSet(bool set) { m_isSet = set; }

}  // namespace snf
