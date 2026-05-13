#pragma once

/**
 * @file ByteArray.h
 * @brief Contiguous byte storage with a write cursor.
 * @ingroup SNFCore_IO
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SNFCore/Span.h"

namespace snf {

/**
 * @class ByteArray
 * @ingroup SNFCore_IO
 * @brief Owns a sequence of bytes and tracks how many have already been consumed.
 *
 * `ByteArray` is used by stream-oriented APIs to keep partially-written payloads
 * in order. When only part of a payload is accepted, `advance()` moves the
 * cursor forward and the next flush continues from the first remaining byte.
 */
class ByteArray
{
public:
    using Storage = std::vector<std::byte>;

    ByteArray() = default;
    explicit ByteArray(Storage bytes) : m_bytes(std::move(bytes)) {}
    explicit ByteArray(Span<const std::byte> bytes) { assign(bytes); }
    explicit ByteArray(const std::vector<std::uint8_t>& bytes)
    {
        assign(uint8BytesView(bytes));
    }
    explicit ByteArray(std::string_view text) { assign(text.data(), text.size()); }
    explicit ByteArray(const std::string& text) : ByteArray(std::string_view(text)) {}
    ByteArray(const void* data, std::size_t size) { assign(bytesView(data, size)); }

    /** @brief Returns the full size of the owned byte sequence. */
    std::size_t size() const noexcept { return m_bytes.size(); }

    /** @brief Returns how many bytes have already been consumed. */
    std::size_t offset() const noexcept { return m_offset; }

    /** @brief Returns how many bytes remain from the current cursor. */
    std::size_t remainingSize() const noexcept { return remainingView().size(); }

    /** @brief Returns `true` when no bytes are owned. */
    bool empty() const noexcept { return m_bytes.empty(); }

    /** @brief Returns `true` once the cursor has reached the end. */
    bool fullyConsumed() const noexcept { return remainingSize() == 0; }

    /** @brief Returns the start of the owned byte sequence, or `nullptr` if empty. */
    const std::byte* data() const noexcept { return bytesView().data(); }

    /** @brief Returns the first remaining byte, or `nullptr` if fully consumed. */
    const std::byte* remainingData() const noexcept { return remainingView().data(); }

    /** @brief Returns the owned storage. */
    const Storage& bytes() const noexcept { return m_bytes; }

    /** @brief Returns the full byte sequence as a non-owning view. */
    Span<const std::byte> bytesView() const noexcept
    {
        return m_bytes.empty() ? Span<const std::byte>() : Span<const std::byte>(m_bytes);
    }

    /** @brief Returns the remaining byte sequence from the current cursor. */
    Span<const std::byte> remainingView() const noexcept { return bytesView().subspan(m_offset); }

    /** @brief Appends @p size bytes from @p data to the end of the storage. */
    void append(Span<const std::byte> bytes)
    {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    void append(const void* data, std::size_t size)
    {
        append(bytesView(data, size));
    }

    /** @brief Appends bytes from @p bytes to the end of the storage. */
    void append(const std::vector<std::uint8_t>& bytes) { append(bytes.data(), bytes.size()); }

    /** @brief Appends text bytes from @p text to the end of the storage. */
    void append(std::string_view text) { append(text.data(), text.size()); }

    /** @brief Moves the cursor forward by at most @p count bytes. */
    void advance(std::size_t count) noexcept { m_offset += std::min(count, remainingSize()); }

    /** @brief Moves the cursor back to the beginning. */
    void reset() noexcept { m_offset = 0; }

    /** @brief Clears all bytes and resets the cursor. */
    void clear() noexcept
    {
        m_bytes.clear();
        m_offset = 0;
    }

    /** @brief Returns remaining bytes interpreted as a string. */
    std::string toString() const
    {
        const Span<const std::byte> remaining = remainingView();
        if (remaining.empty()) {
            return {};
        }

        return std::string(reinterpret_cast<const char*>(remaining.data()), remaining.size());
    }

private:
    static Span<const std::byte> bytesView(const void* data, std::size_t size) noexcept
    {
        if (data == nullptr || size == 0) {
            return {};
        }

        return Span<const std::byte>(static_cast<const std::byte*>(data), size);
    }

    static Span<const std::byte> uint8BytesView(const std::vector<std::uint8_t>& bytes) noexcept
    {
        return Span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
    }

    void assign(Span<const std::byte> bytes)
    {
        m_bytes.assign(bytes.begin(), bytes.end());
        m_offset = 0;
    }

    void assign(const void* data, std::size_t size)
    {
        assign(bytesView(data, size));
    }

private:
    Storage m_bytes;
    std::size_t m_offset = 0;
};

}  // namespace snf
