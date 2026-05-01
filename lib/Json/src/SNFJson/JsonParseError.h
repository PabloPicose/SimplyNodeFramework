#pragma once

/**
 * @file JsonParseError.h
 * @brief Exception raised when JSON text cannot be parsed.
 */

#include <cstddef>
#include <stdexcept>
#include <string>

namespace snf {

/**
 * @class JsonParseError
 * @brief Carries JSON parse diagnostics with byte, line, and column positions.
 */
class JsonParseError : public std::runtime_error
{
public:
    JsonParseError(std::string message, std::size_t byte, std::size_t line, std::size_t column);

    std::size_t byte() const noexcept;
    std::size_t line() const noexcept;
    std::size_t column() const noexcept;

private:
    std::size_t m_byte = 0;
    std::size_t m_line = 0;
    std::size_t m_column = 0;
};

}  // namespace snf
