#pragma once

/**
 * @file ByteArray.h
 * @brief Contiguous byte storage with a write cursor.
 * @ingroup SNFNetwork_IO
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snf {

/**
 * @class ByteArray
 * @ingroup SNFNetwork_IO
 * @brief Owns a sequence of bytes and tracks how many have already been consumed.
 *
 * `ByteArray` is used by stream sockets to keep partially-written payloads in
 * order. When the kernel accepts only part of a payload, `advance()` moves the
 * cursor forward and the next flush continues from the first remaining byte.
 */
class ByteArray
{
public:
    using Storage = std::vector<std::byte>;

    ByteArray() = default;
    explicit ByteArray(Storage bytes) : m_bytes(std::move(bytes)) {}
    explicit ByteArray(const std::vector<std::uint8_t>& bytes)
    {
        assign(bytes.data(), bytes.size());
    }
    explicit ByteArray(std::string_view text) { assign(text.data(), text.size()); }
    explicit ByteArray(const std::string& text) : ByteArray(std::string_view(text)) {}
    ByteArray(const void* data, std::size_t size) { assign(data, size); }

    /** @brief Returns the full size of the owned byte sequence. */
    std::size_t size() const noexcept { return m_bytes.size(); }

    /** @brief Returns how many bytes have already been consumed. */
    std::size_t offset() const noexcept { return m_offset; }

    /** @brief Returns how many bytes remain from the current cursor. */
    std::size_t remainingSize() const noexcept { return m_bytes.size() - m_offset; }

    /** @brief Returns `true` when no bytes are owned. */
    bool empty() const noexcept { return m_bytes.empty(); }

    /** @brief Returns `true` once the cursor has reached the end. */
    bool fullyConsumed() const noexcept { return remainingSize() == 0; }

    /** @brief Returns the start of the owned byte sequence, or `nullptr` if empty. */
    const std::byte* data() const noexcept { return m_bytes.empty() ? nullptr : m_bytes.data(); }

    /** @brief Returns the first remaining byte, or `nullptr` if fully consumed. */
    const std::byte* remainingData() const noexcept
    {
        return fullyConsumed() ? nullptr : (m_bytes.data() + static_cast<std::ptrdiff_t>(m_offset));
    }

    /** @brief Returns the owned storage. */
    const Storage& bytes() const noexcept { return m_bytes; }

    /** @brief Moves the cursor forward by at most @p count bytes. */
    void advance(std::size_t count) noexcept { m_offset += std::min(count, remainingSize()); }

    /** @brief Moves the cursor back to the beginning. */
    void reset() noexcept { m_offset = 0; }

private:
    void assign(const void* data, std::size_t size)
    {
        m_bytes.clear();
        m_offset = 0;

        if (data == nullptr || size == 0) {
            return;
        }

        const auto* first = static_cast<const std::byte*>(data);
        m_bytes.assign(first, first + static_cast<std::ptrdiff_t>(size));
    }

private:
    Storage m_bytes;
    std::size_t m_offset = 0;
};

}  // namespace snf
