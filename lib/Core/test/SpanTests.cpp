#include <gtest/gtest.h>

#include "SNFCore/Span.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace snf;

namespace {

std::vector<std::uint8_t> toUInt8Vector(Span<const std::byte> bytes)
{
    std::vector<std::uint8_t> output;
    output.reserve(bytes.size());
    for (const std::byte value : bytes) {
        output.push_back(static_cast<std::uint8_t>(value));
    }
    return output;
}

}  // namespace

TEST(SpanTests, defaultConstructionCreatesEmptyView)
{
    Span<const std::byte> bytes;

    EXPECT_TRUE(bytes.empty());
    EXPECT_EQ(bytes.size(), 0U);
    EXPECT_EQ(bytes.data(), nullptr);
    EXPECT_EQ(bytes.begin(), bytes.end());
}

TEST(SpanTests, pointerAndSizeConstructionExposesContiguousBytes)
{
    const std::array<std::byte, 4> source{
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
    };

    const Span<const std::byte> bytes(source.data(), source.size());

    EXPECT_EQ(bytes.size(), source.size());
    EXPECT_EQ(bytes.front(), std::byte{0x01});
    EXPECT_EQ(bytes.back(), std::byte{0x04});
    EXPECT_EQ(toUInt8Vector(bytes), (std::vector<std::uint8_t>{0x01, 0x02, 0x03, 0x04}));
}

TEST(SpanTests, arrayAndVectorConstructionExposesReadOnlyViews)
{
    std::array<std::byte, 3> array{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    std::vector<std::byte> vector{std::byte{0xAA}, std::byte{0xBB}};

    const Span<const std::byte> arrayView(array);
    const Span<const std::byte> vectorView(vector);

    ASSERT_EQ(arrayView.size(), 3U);
    ASSERT_EQ(vectorView.size(), 2U);

    EXPECT_EQ(arrayView[1], std::byte{0x20});
    EXPECT_EQ(vectorView[0], std::byte{0xAA});
    EXPECT_EQ(vectorView[1], std::byte{0xBB});
}

TEST(SpanTests, subspanFirstAndLastUseValidRanges)
{
    const std::array<std::byte, 5> source{
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
    };
    const Span<const std::byte> bytes(source);

    EXPECT_EQ(toUInt8Vector(bytes.first(source.size())),
        (std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03, 0x04}));
    EXPECT_EQ(toUInt8Vector(bytes.last(2)), (std::vector<std::uint8_t>{0x03, 0x04}));
    EXPECT_EQ(toUInt8Vector(bytes.subspan(1, 3)), (std::vector<std::uint8_t>{0x01, 0x02, 0x03}));
    EXPECT_TRUE(bytes.subspan(source.size()).empty());
    EXPECT_EQ(bytes.subspan(source.size()).data(), source.data() + source.size());
}