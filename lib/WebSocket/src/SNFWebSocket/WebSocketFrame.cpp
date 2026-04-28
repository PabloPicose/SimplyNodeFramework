#include "SNFWebSocket/WebSocketFrame.h"

#include <algorithm>
#include <array>
#include <limits>
#include <random>
#include <utility>

namespace snf::websocket::detail {

namespace {

bool isKnownOpcode(std::uint8_t opcode)
{
    switch (static_cast<OpCode>(opcode)) {
    case OpCode::Continuation:
    case OpCode::Text:
    case OpCode::Binary:
    case OpCode::Close:
    case OpCode::Ping:
    case OpCode::Pong:
        return true;
    }
    return false;
}

bool isControlOpcode(OpCode opcode)
{
    return opcode == OpCode::Close || opcode == OpCode::Ping || opcode == OpCode::Pong;
}

std::uint64_t readBigEndian(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t count)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        value = (value << 8u) | data[offset + i];
    }
    return value;
}

std::array<std::uint8_t, 4> randomMask()
{
    static thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> distribution(0, 255);

    return {static_cast<std::uint8_t>(distribution(generator)),
            static_cast<std::uint8_t>(distribution(generator)),
            static_cast<std::uint8_t>(distribution(generator)),
            static_cast<std::uint8_t>(distribution(generator))};
}

}  // namespace

void WebSocketFrameParser::feed(const std::uint8_t* data, std::size_t size)
{
    if (hasError() || size == 0) {
        return;
    }

    m_buffer.insert(m_buffer.end(), data, data + size);
    while (!hasError() && tryParseOne()) {
    }
}

void WebSocketFrameParser::feed(const std::vector<std::uint8_t>& data)
{
    feed(data.data(), data.size());
}

bool WebSocketFrameParser::hasFrame() const
{
    return !m_frames.empty();
}

WebSocketFrame WebSocketFrameParser::nextFrame()
{
    if (m_frames.empty()) {
        return {};
    }

    WebSocketFrame frame = std::move(m_frames.front());
    m_frames.erase(m_frames.begin());
    return frame;
}

bool WebSocketFrameParser::hasError() const
{
    return !m_error.empty();
}

const std::string& WebSocketFrameParser::error() const
{
    return m_error;
}

void WebSocketFrameParser::reset()
{
    m_buffer.clear();
    m_frames.clear();
    m_error.clear();
}

bool WebSocketFrameParser::tryParseOne()
{
    if (m_buffer.size() < 2) {
        return false;
    }

    const std::uint8_t first = m_buffer[0];
    const std::uint8_t second = m_buffer[1];
    const bool fin = (first & 0x80u) != 0;
    const bool hasReservedBits = (first & 0x70u) != 0;
    const auto opcodeByte = static_cast<std::uint8_t>(first & 0x0Fu);
    const bool masked = (second & 0x80u) != 0;
    std::uint64_t payloadLength = static_cast<std::uint64_t>(second & 0x7Fu);

    if (hasReservedBits) {
        setError("Reserved frame bits are not supported");
        return false;
    }

    if (!isKnownOpcode(opcodeByte)) {
        setError("Unsupported WebSocket opcode");
        return false;
    }

    const auto opcode = static_cast<OpCode>(opcodeByte);

    std::size_t offset = 2;
    if (payloadLength == 126) {
        if (m_buffer.size() < offset + 2) {
            return false;
        }
        payloadLength = readBigEndian(m_buffer, offset, 2);
        offset += 2;
        if (payloadLength < 126) {
            setError("Non-minimal 16-bit WebSocket payload length");
            return false;
        }
    } else if (payloadLength == 127) {
        if (m_buffer.size() < offset + 8) {
            return false;
        }
        payloadLength = readBigEndian(m_buffer, offset, 8);
        offset += 8;
        if ((payloadLength & (1ull << 63u)) != 0) {
            setError("Invalid 64-bit WebSocket payload length");
            return false;
        }
        if (payloadLength <= 0xFFFFu) {
            setError("Non-minimal 64-bit WebSocket payload length");
            return false;
        }
    }

    if (isControlOpcode(opcode)) {
        if (!fin) {
            setError("Fragmented WebSocket control frames are invalid");
            return false;
        }
        if (payloadLength > 125) {
            setError("WebSocket control frame payload is too large");
            return false;
        }
    }

    std::array<std::uint8_t, 4> maskKey{};
    if (masked) {
        if (m_buffer.size() < offset + maskKey.size()) {
            return false;
        }
        std::copy_n(m_buffer.begin() + static_cast<std::ptrdiff_t>(offset), maskKey.size(), maskKey.begin());
        offset += maskKey.size();
    }

    if (payloadLength > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        setError("WebSocket payload is too large for this platform");
        return false;
    }

    const auto payloadSize = static_cast<std::size_t>(payloadLength);
    if (m_buffer.size() < offset + payloadSize) {
        return false;
    }

    WebSocketFrame frame;
    frame.fin = fin;
    frame.opcode = opcode;
    frame.masked = masked;
    frame.payload.assign(m_buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                         m_buffer.begin() + static_cast<std::ptrdiff_t>(offset + payloadSize));

    if (masked) {
        for (std::size_t i = 0; i < frame.payload.size(); ++i) {
            frame.payload[i] ^= maskKey[i % maskKey.size()];
        }
    }

    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(offset + payloadSize));
    m_frames.push_back(std::move(frame));
    return true;
}

void WebSocketFrameParser::setError(std::string message)
{
    m_error = std::move(message);
}

std::vector<std::uint8_t> encodeFrame(OpCode opcode,
                                      const std::vector<std::uint8_t>& payload,
                                      bool maskPayload)
{
    std::vector<std::uint8_t> output;
    output.reserve(payload.size() + 14);

    output.push_back(static_cast<std::uint8_t>(0x80u | static_cast<std::uint8_t>(opcode)));

    const std::uint8_t maskBit = maskPayload ? 0x80u : 0x00u;
    if (payload.size() <= 125) {
        output.push_back(static_cast<std::uint8_t>(maskBit | payload.size()));
    } else if (payload.size() <= 0xFFFFu) {
        output.push_back(static_cast<std::uint8_t>(maskBit | 126u));
        output.push_back(static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu));
        output.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
    } else {
        output.push_back(static_cast<std::uint8_t>(maskBit | 127u));
        const auto length = static_cast<std::uint64_t>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8) {
            output.push_back(static_cast<std::uint8_t>((length >> static_cast<unsigned>(shift)) & 0xFFu));
        }
    }

    if (!maskPayload) {
        output.insert(output.end(), payload.begin(), payload.end());
        return output;
    }

    const auto mask = randomMask();
    output.insert(output.end(), mask.begin(), mask.end());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        output.push_back(static_cast<std::uint8_t>(payload[i] ^ mask[i % mask.size()]));
    }

    return output;
}

}  // namespace snf::websocket::detail
