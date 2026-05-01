#include "SNFJson/JsonParseError.h"

#include <utility>

namespace snf {

JsonParseError::JsonParseError(std::string message, std::size_t byte, std::size_t line, std::size_t column)
    : std::runtime_error(std::move(message))
    , m_byte(byte)
    , m_line(line)
    , m_column(column)
{
}

std::size_t JsonParseError::byte() const noexcept
{
    return m_byte;
}

std::size_t JsonParseError::line() const noexcept
{
    return m_line;
}

std::size_t JsonParseError::column() const noexcept
{
    return m_column;
}

}  // namespace snf
