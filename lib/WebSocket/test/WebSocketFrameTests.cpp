#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

#include <algorithm>
#include <string>
#include <vector>

#include "SNFWebSocket/WebSocketFrame.h"

using namespace snf::websocket::detail;

namespace {

std::vector<std::uint8_t> bytes(const std::string& text)
{
    return {text.begin(), text.end()};
}

}  // namespace

TEST(WebSocketFrameParserTest, ParsesUnmaskedTextFrameIncrementally)
{
    const auto encoded = encodeFrame(OpCode::Text, bytes("hello"), false);

    WebSocketFrameParser parser;
    parser.feed(encoded.data(), 2);
    EXPECT_FALSE(parser.hasFrame());
    parser.feed(encoded.data() + 2, encoded.size() - 2);

    ASSERT_TRUE(parser.hasFrame());
    const WebSocketFrame frame = parser.nextFrame();
    EXPECT_TRUE(frame.fin);
    EXPECT_EQ(frame.opcode, OpCode::Text);
    EXPECT_FALSE(frame.masked);
    EXPECT_EQ(frame.payload, bytes("hello"));
}

TEST(WebSocketFrameParserTest, EncodesMaskedClientFrame)
{
    const auto encoded = encodeFrame(OpCode::Text, bytes("masked"), true);

    ASSERT_GE(encoded.size(), 2u + 4u);
    EXPECT_NE(encoded[1] & 0x80u, 0);

    WebSocketFrameParser parser;
    parser.feed(encoded);

    ASSERT_TRUE(parser.hasFrame());
    const WebSocketFrame frame = parser.nextFrame();
    EXPECT_TRUE(frame.masked);
    EXPECT_EQ(frame.payload, bytes("masked"));
}

TEST(WebSocketFrameParserTest, HandlesPayloadLength125)
{
    std::vector<std::uint8_t> payload(125, 'a');
    const auto encoded = encodeFrame(OpCode::Binary, payload, false);

    EXPECT_EQ(encoded[1] & 0x7Fu, 125);

    WebSocketFrameParser parser;
    parser.feed(encoded);

    ASSERT_TRUE(parser.hasFrame());
    const WebSocketFrame frame = parser.nextFrame();
    EXPECT_EQ(frame.opcode, OpCode::Binary);
    EXPECT_EQ(frame.payload, payload);
}

TEST(WebSocketFrameParserTest, HandlesExtended16BitPayloadLength)
{
    std::vector<std::uint8_t> payload(126, 'b');
    const auto encoded = encodeFrame(OpCode::Binary, payload, false);

    ASSERT_GE(encoded.size(), 4u);
    EXPECT_EQ(encoded[1] & 0x7Fu, 126);
    EXPECT_EQ(encoded[2], 0);
    EXPECT_EQ(encoded[3], 126);

    WebSocketFrameParser parser;
    parser.feed(encoded);

    ASSERT_TRUE(parser.hasFrame());
    EXPECT_EQ(parser.nextFrame().payload, payload);
}

TEST(WebSocketFrameParserTest, HandlesExtended64BitPayloadLength)
{
    std::vector<std::uint8_t> payload(66000, 'c');
    const auto encoded = encodeFrame(OpCode::Binary, payload, false);

    ASSERT_GE(encoded.size(), 10u);
    EXPECT_EQ(encoded[1] & 0x7Fu, 127);

    WebSocketFrameParser parser;
    parser.feed(encoded.data(), 10);
    EXPECT_FALSE(parser.hasFrame());
    parser.feed(encoded.data() + 10, encoded.size() - 10);

    ASSERT_TRUE(parser.hasFrame());
    EXPECT_EQ(parser.nextFrame().payload, payload);
}

TEST(WebSocketFrameParserTest, RejectsReservedBits)
{
    const std::vector<std::uint8_t> invalid = {0xC1, 0x00};

    WebSocketFrameParser parser;
    parser.feed(invalid);

    EXPECT_TRUE(parser.hasError());
}

TEST(WebSocketFrameParserTest, ManySmallFramesInOneChunkParseInLinearTime)
{
    // Autobahn 9.3.x: a 4 MiB message split into 64-byte fragments. The
    // parser must not shift its whole buffer (or frame queue) per frame.
    constexpr std::size_t kFrames = 65536;
    const std::vector<std::uint8_t> fragment(64, 'f');
    std::vector<std::uint8_t> wire;
    wire.reserve(kFrames * (fragment.size() + 6));
    for (std::size_t i = 0; i < kFrames; ++i) {
        const auto frame = encodeFrame(i == 0 ? OpCode::Text : OpCode::Continuation, fragment, true);
        wire.insert(wire.end(), frame.begin(), frame.end());
    }

    WebSocketFrameParser parser;
    const auto start = std::chrono::steady_clock::now();
    parser.feed(wire);
    std::size_t parsed = 0;
    while (parser.hasFrame()) {
        parser.nextFrame();
        ++parsed;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(parsed, kFrames);
    EXPECT_FALSE(parser.hasError());
    // Linear parsing takes a few ms even in Debug; the quadratic version took seconds.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 1000);
}

TEST(WebSocketFrameParserTest, FrameSplitAcrossManyFeedsIsReassembled)
{
    const std::vector<std::uint8_t> payload(100000, 'p');
    const auto wire = encodeFrame(OpCode::Binary, payload, true);
    WebSocketFrameParser parser;
    for (std::size_t i = 0; i < wire.size(); i += 7) {
        parser.feed(wire.data() + i, std::min<std::size_t>(7, wire.size() - i));
    }
    ASSERT_TRUE(parser.hasFrame());
    const auto frame = parser.nextFrame();
    EXPECT_EQ(frame.payload, payload);
    EXPECT_FALSE(parser.hasFrame());
}
