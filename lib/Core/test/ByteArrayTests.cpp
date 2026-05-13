#include <gtest/gtest.h>

#include "SNFCore/ByteArray.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace snf;

namespace {

std::vector<std::uint8_t> toUInt8Vector(const ByteArray& bytes)
{
    std::vector<std::uint8_t> output;
    output.reserve(bytes.bytes().size());
    for (const std::byte value : bytes.bytes()) {
        output.push_back(static_cast<std::uint8_t>(value));
    }
    return output;
}

}  // namespace

TEST(ByteArrayTests, defaultConstructionIsEmpty)
{
    ByteArray bytes;

    EXPECT_TRUE(bytes.empty());
    EXPECT_EQ(bytes.size(), 0U);
    EXPECT_EQ(bytes.offset(), 0U);
    EXPECT_EQ(bytes.remainingSize(), 0U);
    EXPECT_TRUE(bytes.fullyConsumed());
    EXPECT_EQ(bytes.data(), nullptr);
    EXPECT_EQ(bytes.remainingData(), nullptr);
    EXPECT_EQ(bytes.toString(), "");
}

TEST(ByteArrayTests, stringConstructorStoresText)
{
    ByteArray bytes(std::string("hello"));

    EXPECT_FALSE(bytes.empty());
    EXPECT_EQ(bytes.size(), 5U);
    EXPECT_EQ(bytes.offset(), 0U);
    EXPECT_EQ(bytes.remainingSize(), 5U);
    EXPECT_EQ(bytes.toString(), "hello");
}

TEST(ByteArrayTests, vectorConstructorStoresBinaryBytes)
{
    const std::vector<std::uint8_t> expected{0x00, 0x7F, 0x80, 0xFF};
    ByteArray bytes(expected);

    EXPECT_EQ(bytes.size(), expected.size());
    EXPECT_EQ(toUInt8Vector(bytes), expected);
}

TEST(ByteArrayTests, spanConstructorStoresBinaryBytes)
{
    const std::array<std::byte, 4> expected{
        std::byte{0x00},
        std::byte{0x7F},
        std::byte{0x80},
        std::byte{0xFF},
    };
    ByteArray bytes{Span<const std::byte>(expected)};

    EXPECT_EQ(bytes.size(), expected.size());
    EXPECT_EQ(bytes.bytesView().size(), expected.size());
    EXPECT_EQ(bytes.bytesView().data(), bytes.data());
    EXPECT_EQ(bytes.bytesView()[0], std::byte{0x00});
    EXPECT_EQ(bytes.bytesView()[3], std::byte{0xFF});
}

TEST(ByteArrayTests, advanceUpdatesRemainingViewWithoutOverflow)
{
    ByteArray bytes(std::string("abcdef"));

    bytes.advance(2);
    EXPECT_EQ(bytes.offset(), 2U);
    EXPECT_EQ(bytes.remainingSize(), 4U);
    EXPECT_FALSE(bytes.fullyConsumed());
    EXPECT_EQ(bytes.toString(), "cdef");
    EXPECT_EQ(bytes.remainingView().size(), 4U);
    EXPECT_EQ(bytes.remainingView()[0], std::byte{'c'});
    EXPECT_EQ(bytes.remainingView()[3], std::byte{'f'});

    bytes.advance(50);
    EXPECT_EQ(bytes.offset(), 6U);
    EXPECT_EQ(bytes.remainingSize(), 0U);
    EXPECT_TRUE(bytes.fullyConsumed());
    EXPECT_EQ(bytes.remainingData(), bytes.data() + static_cast<std::ptrdiff_t>(bytes.size()));
    EXPECT_EQ(bytes.remainingView().data(), bytes.data() + static_cast<std::ptrdiff_t>(bytes.size()));
    EXPECT_EQ(bytes.toString(), "");
}

TEST(ByteArrayTests, resetRestoresCursorToBeginning)
{
    ByteArray bytes(std::string("abcdef"));

    bytes.advance(3);
    ASSERT_EQ(bytes.toString(), "def");

    bytes.reset();
    EXPECT_EQ(bytes.offset(), 0U);
    EXPECT_EQ(bytes.remainingSize(), 6U);
    EXPECT_EQ(bytes.toString(), "abcdef");
}

TEST(ByteArrayTests, clearRemovesAllBytesAndResetsCursor)
{
    ByteArray bytes(std::string("abcdef"));
    bytes.advance(2);

    bytes.clear();

    EXPECT_TRUE(bytes.empty());
    EXPECT_EQ(bytes.size(), 0U);
    EXPECT_EQ(bytes.offset(), 0U);
    EXPECT_EQ(bytes.remainingSize(), 0U);
    EXPECT_EQ(bytes.data(), nullptr);
    EXPECT_EQ(bytes.remainingData(), nullptr);
}

TEST(ByteArrayTests, appendStringAppendsAtEndWithoutChangingCursor)
{
    ByteArray bytes(std::string("abc"));
    bytes.advance(1);

    bytes.append(std::string_view("def"));

    EXPECT_EQ(bytes.size(), 6U);
    EXPECT_EQ(bytes.offset(), 1U);
    EXPECT_EQ(bytes.remainingSize(), 5U);
    EXPECT_EQ(bytes.toString(), "bcdef");
}

TEST(ByteArrayTests, appendVectorAppendsBinaryPayload)
{
    ByteArray bytes(std::string("ab"));
    const std::vector<std::uint8_t> tail{0x01, 0x02, 0x03};

    bytes.append(tail);

    const std::vector<std::uint8_t> expected{'a', 'b', 0x01, 0x02, 0x03};
    EXPECT_EQ(toUInt8Vector(bytes), expected);
}

TEST(ByteArrayTests, appendSpanAppendsBinaryPayload)
{
    ByteArray bytes(std::string("ab"));
    const std::array<std::byte, 4> tail{
        std::byte{'x'},
        std::byte{0x00},
        std::byte{'y'},
        std::byte{'z'},
    };

    bytes.append(Span<const std::byte>(tail));

    const std::vector<std::uint8_t> expected{'a', 'b', 'x', 0x00, 'y', 'z'};
    EXPECT_EQ(toUInt8Vector(bytes), expected);
    EXPECT_EQ(bytes.toString().size(), 6U);
    EXPECT_EQ(bytes.toString()[3], '\0');
}

TEST(ByteArrayTests, emptyViewsExposeNullPointers)
{
    ByteArray bytes;

    EXPECT_EQ(bytes.bytesView().data(), nullptr);
    EXPECT_EQ(bytes.remainingView().data(), nullptr);
    EXPECT_TRUE(bytes.bytesView().empty());
    EXPECT_TRUE(bytes.remainingView().empty());
}

TEST(ByteArrayTests, appendRawMemorySupportsEmbeddedNullBytes)
{
    const std::array<char, 4> tail{'x', '\0', 'y', 'z'};
    ByteArray bytes(std::string("ab"));

    bytes.append(tail.data(), tail.size());

    EXPECT_EQ(bytes.size(), 6U);
    EXPECT_EQ(bytes.remainingSize(), 6U);
    EXPECT_EQ(bytes.toString().size(), 6U);
    EXPECT_EQ(bytes.toString()[0], 'a');
    EXPECT_EQ(bytes.toString()[1], 'b');
    EXPECT_EQ(bytes.toString()[2], 'x');
    EXPECT_EQ(bytes.toString()[3], '\0');
    EXPECT_EQ(bytes.toString()[4], 'y');
    EXPECT_EQ(bytes.toString()[5], 'z');
}

TEST(ByteArrayTests, byteStorageConstructorPreservesExactBytes)
{
    ByteArray::Storage storage;
    storage.push_back(std::byte{0x10});
    storage.push_back(std::byte{0x20});
    storage.push_back(std::byte{0x30});

    ByteArray bytes(std::move(storage));

    const std::vector<std::uint8_t> expected{0x10, 0x20, 0x30};
    EXPECT_EQ(toUInt8Vector(bytes), expected);
}