#pragma once

/**
 * @file Span.h
 * @brief Lightweight contiguous view for C++17 builds.
 * @ingroup SNFCore_IO
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace snf {

/**
 * @class Span
 * @ingroup SNFCore_IO
 * @brief Non-owning view over a contiguous range of elements.
 */
template<typename T>
class Span
{
public:
    static_assert(std::is_const<T>::value,
        "snf::Span only supports const element types. Use Span<const T>.");

    /** @brief Element type of the span. */
    using element_type = T;
    /** @brief Non-cv element type. */
    using value_type = typename std::remove_cv<T>::type;
    /** @brief Size type used for lengths and indexes. */
    using size_type = std::size_t;
    /** @brief Signed type used for iterator differences. */
    using difference_type = std::ptrdiff_t;
    /** @brief Pointer to span element type. */
    using pointer = const value_type*;
    /** @brief Const pointer to span element type. */
    using const_pointer = const value_type*;
    /** @brief Reference to span element type. */
    using reference = const value_type&;
    /** @brief Const reference to span element type. */
    using const_reference = const T&;
    /** @brief Iterator type for contiguous traversal. */
    using iterator = pointer;
    /** @brief Const iterator type for contiguous traversal. */
    using const_iterator = const_pointer;

    /** @brief Sentinel value used as "until end" in subspan operations. */
    static constexpr size_type npos = static_cast<size_type>(-1);

    /** @brief Constructs an empty span. */
    constexpr Span() noexcept = default;

    /** @brief Constructs a span from raw pointer and element count. */
    constexpr Span(pointer data, size_type size) noexcept
        : m_data(data)
        , m_size(size)
    {
    }

    /** @brief Constructs a span from a C array. */
    template<size_type Extent>
    constexpr Span(const value_type (&array)[Extent]) noexcept
        : m_data(array)
        , m_size(Extent)
    {
    }

    /** @brief Constructs a span from a mutable std::array when element types are convertible. */
    template<typename U, size_type Extent,
             typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    constexpr Span(std::array<U, Extent>& array) noexcept
        : m_data(array.data())
        , m_size(array.size())
    {
    }

    /** @brief Constructs a span from a const std::array when element types are convertible. */
    template<typename U, size_type Extent,
             typename = typename std::enable_if<std::is_convertible<const U*, T*>::value>::type>
    constexpr Span(const std::array<U, Extent>& array) noexcept
        : m_data(array.data())
        , m_size(array.size())
    {
    }

    /** @brief Constructs a span from a mutable std::vector when element types are convertible. */
    template<typename U,
             typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    Span(std::vector<U>& vector) noexcept
        : m_data(vector.data())
        , m_size(vector.size())
    {
    }

    /** @brief Constructs a span from a const std::vector when element types are convertible. */
    template<typename U,
             typename = typename std::enable_if<std::is_convertible<const U*, T*>::value>::type>
    Span(const std::vector<U>& vector) noexcept
        : m_data(vector.data())
        , m_size(vector.size())
    {
    }

    /** @brief Constructs a span from another span with convertible element type. */
    template<typename U,
             typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    constexpr Span(const Span<U>& other) noexcept
        : m_data(other.data())
        , m_size(other.size())
    {
    }

    /** @brief Returns iterator to the first element. */
    constexpr iterator begin() const noexcept
    {
        return m_data;
    }

    /** @brief Returns iterator one past the last element. */
    constexpr iterator end() const noexcept
    {
        return m_data + static_cast<difference_type>(m_size);
    }

    /** @brief Returns const iterator to the first element. */
    constexpr const_iterator cbegin() const noexcept
    {
        return m_data;
    }

    /** @brief Returns const iterator one past the last element. */
    constexpr const_iterator cend() const noexcept
    {
        return m_data + static_cast<difference_type>(m_size);
    }

    /** @brief Returns element reference at @p index. */
    constexpr reference operator[](size_type index) const noexcept
    {
        return m_data[index];
    }

    /** @brief Returns reference to first element. Requires non-empty span. */
    constexpr reference front() const noexcept
    {
        assert(!empty());
        return m_data[0];
    }

    /** @brief Returns reference to last element. Requires non-empty span. */
    constexpr reference back() const noexcept
    {
        assert(!empty());
        return m_data[m_size - 1];
    }

    /** @brief Returns pointer to first element (or one-past for certain empty subspans). */
    constexpr pointer data() const noexcept
    {
        return m_data;
    }

    /** @brief Returns number of elements in the span. */
    constexpr size_type size() const noexcept
    {
        return m_size;
    }

    /** @brief Returns total byte size of the span. */
    constexpr size_type size_bytes() const noexcept
    {
        return m_size * sizeof(element_type);
    }

    /** @brief Returns true when span has no elements. */
    constexpr bool empty() const noexcept
    {
        return m_size == 0;
    }

    /**
     * @brief Returns first @p count elements.
     * @pre @p count <= size().
     */
    constexpr Span first(size_type count) const noexcept
    {
        assert(count <= m_size);
        return Span(m_data, count);
    }

    /**
     * @brief Returns last @p count elements.
     * @pre @p count <= size().
     */
    constexpr Span last(size_type count) const noexcept
    {
        assert(count <= m_size);
        return Span(m_data + static_cast<difference_type>(m_size - count), count);
    }

    /**
     * @brief Returns a subspan starting at @p offset with optional @p count elements.
     * @param offset Start position in current span.
     * @param count Number of elements to include, or npos to use remaining elements.
     * @pre @p offset <= size().
     * @pre If @p count != npos, then @p count <= size() - @p offset.
     */
    constexpr Span subspan(size_type offset, size_type count = npos) const noexcept
    {
        assert(offset <= m_size);

        const size_type available = m_size - offset;

        if (count == npos) {
            return Span(m_data + static_cast<difference_type>(offset), available);
        }

        assert(count <= available);
        return Span(m_data + static_cast<difference_type>(offset), count);
    }

private:
    pointer m_data = nullptr;
    size_type m_size = 0;
};

/**
 * @brief Returns a read-only byte view over a typed contiguous span.
 * @note The resulting size is expressed in bytes.
 */
template<typename T>
Span<const std::byte> asBytes(Span<T> span) noexcept
{
    return Span<const std::byte>{
        reinterpret_cast<const std::byte*>(span.data()),
        span.size_bytes()
    };
}

}  // namespace snf