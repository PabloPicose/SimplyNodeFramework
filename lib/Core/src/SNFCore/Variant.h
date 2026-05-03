#pragma once

/**
 * @file Variant.h
 * @brief Type-safe value container for heterogeneous data in SNF models.
 * @ingroup SNFCore
 */

#include <cstdint>
#include <string>
#include <variant>

namespace snf {

/**
 * @struct ModelColor
 * @ingroup SNFCore
 * @brief RGBA color value used by decoration roles.
 *
 * Components are expected in the `[0, 1]` range. Views may clamp values when
 * converting to their backend color representation.
 */
struct ModelColor
{
    float red   = 0.0f;
    float green = 0.0f;
    float blue  = 0.0f;
    float alpha = 1.0f;

    friend bool operator==(const ModelColor& lhs, const ModelColor& rhs)
    {
        return lhs.red == rhs.red
            && lhs.green == rhs.green
            && lhs.blue == rhs.blue
            && lhs.alpha == rhs.alpha;
    }

    friend bool operator!=(const ModelColor& lhs, const ModelColor& rhs) { return ! (lhs == rhs); }
};

/**
 * @class Variant
 * @ingroup SNFCore
 * @brief Qt-style type-safe value container.
 *
 * Stores one of the following types at a time:
 * - **Null** — no value (default-constructed state)
 * - **Bool** — `bool`
 * - **Int64** — `std::int64_t`; convenience constructors from `int` and
 *   `std::int32_t` are provided that widen to `int64`.
 * - **Double** — `double`
 * - **String** — `std::string`
 * - **Color** — `ModelColor`
 *
 * Implicit construction from any of the listed types is supported, enabling
 * model `data()` implementations to `return` raw values directly:
 * @code
 * snf::Variant data(const snf::ModelIndex& index, snf::ModelDataRole) const override
 * {
 *     if (index.column() == 0) return std::int64_t{42};
 *     if (index.column() == 1) return std::string{"hello"};
 *     return {};  // Null
 * }
 * @endcode
 *
 * Typed access uses either the @c toXxx() conversion helpers or the
 * template @c holds<T>() / @c get<T>() methods for direct inspection:
 * @code
 * snf::Variant v(std::int64_t{7});
 * assert(v.type() == snf::Variant::Type::Int64);
 * assert(v.toInt64() == 7);
 * assert(v.holds<std::int64_t>());
 * assert(*v.get<std::int64_t>() == 7);
 * @endcode
 */
class Variant
{
public:
    /**
     * @enum Type
     * @brief Discriminator for the value currently stored.
     */
    enum class Type {
        Null,   ///< No value (std::monostate).
        Bool,   ///< Boolean value.
        Int64,  ///< 64-bit signed integer.
        Double, ///< Double-precision floating-point number.
        String, ///< UTF-8 text string.
        Color,  ///< RGBA color (ModelColor).
    };

    // ── Constructors ──────────────────────────────────────────────────────

    /** @brief Constructs a Null variant. */
    Variant() noexcept = default;

    /** @brief Constructs a Bool variant. */
    /* implicit */ Variant(bool value) noexcept;

    /** @brief Constructs an Int64 variant from an @c int, widening to 64-bit. */
    /* implicit */ Variant(int value) noexcept;

    /** @brief Constructs an Int64 variant. */
    /* implicit */ Variant(std::int64_t value) noexcept;

    /** @brief Constructs a Double variant. */
    /* implicit */ Variant(double value) noexcept;

    /** @brief Constructs a String variant from an @c std::string. */
    /* implicit */ Variant(std::string value);

    /** @brief Constructs a String variant from a C-string literal. */
    /* implicit */ Variant(const char* value);

    /** @brief Constructs a Color variant. */
    /* implicit */ Variant(ModelColor value) noexcept;

    // ── Type query ────────────────────────────────────────────────────────

    /** @brief Returns the type discriminator for the stored value. */
    Type type() const noexcept;

    /** @brief Returns @c true if no value is stored (Null). */
    bool isNull() const noexcept;

    /** @brief Returns @c true if a non-Null value is stored. */
    bool isValid() const noexcept;

    // ── Typed access ──────────────────────────────────────────────────────

    /**
     * @brief Returns @c true if the stored value is exactly of type @p T.
     *
     * @p T must be one of `bool`, `std::int64_t`, `double`, `std::string`,
     * `ModelColor`, or `std::monostate`.
     */
    template<typename T>
    bool holds() const noexcept
    {
        return std::holds_alternative<T>(m_value);
    }

    /**
     * @brief Returns a pointer to the stored value if it is of type @p T,
     * or @c nullptr otherwise.
     */
    template<typename T>
    const T* get() const noexcept
    {
        return std::get_if<T>(&m_value);
    }

    // ── Conversion helpers ────────────────────────────────────────────────

    /**
     * @brief Converts the stored value to @c bool.
     *
     * - Bool  → stored value
     * - Int64 → @c value != 0
     * - Double → @c value != 0.0
     * - String → @c "true" or @c "1"
     * - All others → @p defaultValue
     */
    bool toBool(bool defaultValue = false) const noexcept;

    /**
     * @brief Converts the stored value to @c std::int64_t.
     *
     * - Int64  → stored value
     * - Bool   → 0 or 1
     * - Double → truncated cast
     * - String → parsed via `std::stoll`; @p defaultValue on parse failure
     * - All others → @p defaultValue
     */
    std::int64_t toInt64(std::int64_t defaultValue = 0) const noexcept;

    /**
     * @brief Converts the stored value to @c double.
     *
     * - Double → stored value
     * - Int64  → widened cast
     * - Bool   → 0.0 or 1.0
     * - String → parsed via `std::stod`; @p defaultValue on parse failure
     * - All others → @p defaultValue
     */
    double toDouble(double defaultValue = 0.0) const noexcept;

    /**
     * @brief Converts the stored value to a display string.
     *
     * - Null   → `""`
     * - Bool   → `"true"` or `"false"`
     * - Int64  → decimal representation
     * - Double → shortest decimal representation
     * - String → stored value
     * - Color  → `""`
     */
    std::string toString() const;

    /**
     * @brief Returns the stored ModelColor, or a default-constructed one if
     * the variant is not a Color.
     */
    ModelColor toColor() const noexcept;

    // ── Comparison ────────────────────────────────────────────────────────

    bool operator==(const Variant& other) const;
    bool operator!=(const Variant& other) const;

private:
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, ModelColor>;
    Storage m_value;
};

}  // namespace snf
