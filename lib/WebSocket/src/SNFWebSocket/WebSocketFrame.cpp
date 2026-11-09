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

std::uint64_t readBigEndian(const ByteArray::Storage& data, std::size_t offset, std::size_t count)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        value = (value << 8u) | std::to_integer<std::uint8_t>(data[offset + i]);
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
    feed(Span<const std::byte>(reinterpret_cast<const std::byte*>(data), size));
}

void WebSocketFrameParser::feed(const std::vector<std::uint8_t>& data)
{
    feed(Span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()));
}

void WebSocketFrameParser::feed(Span<const std::byte> data)
{
    if (hasError() || data.empty()) {
        return;
    }

    m_buffer.insert(m_buffer.end(), data.begin(), data.end());
    while (!hasError() && tryParseOne()) {
    }

    // Drop consumed bytes once per feed() instead of once per frame, which
    // would make many small frames quadratic in the buffered size.
    if (m_offset == m_buffer.size()) {
        m_buffer.clear();
    } else if (m_offset > 0) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset));
    }
    m_offset = 0;
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
    m_frames.pop_front();
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
    m_offset = 0;
    m_frames.clear();
    m_error.clear();
}

bool WebSocketFrameParser::tryParseOne()
{
    const std::size_t available = m_buffer.size() - m_offset;
    if (available < 2) {
        return false;
    }

    const std::uint8_t first = std::to_integer<std::uint8_t>(m_buffer[m_offset]);
    const std::uint8_t second = std::to_integer<std::uint8_t>(m_buffer[m_offset + 1]);
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
        if (available < offset + 2) {
            return false;
        }
        payloadLength = readBigEndian(m_buffer, m_offset + offset, 2);
        offset += 2;
        if (payloadLength < 126) {
            setError("Non-minimal 16-bit WebSocket payload length");
            return false;
        }
    } else if (payloadLength == 127) {
        if (available < offset + 8) {
            return false;
        }
        payloadLength = readBigEndian(m_buffer, m_offset + offset, 8);
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
        if (available < offset + maskKey.size()) {
            return false;
        }
        std::copy_n(reinterpret_cast<const std::uint8_t*>(m_buffer.data() + m_offset + offset),
                maskKey.size(), maskKey.begin());
        offset += maskKey.size();
    }

    if (payloadLength > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        setError("WebSocket payload is too large for this platform");
        return false;
    }

    const auto payloadSize = static_cast<std::size_t>(payloadLength);
    if (available - offset < payloadSize) {
        return false;
    }

    const std::byte* payloadStart = m_buffer.data() + m_offset + offset;
    WebSocketFrame frame;
    frame.fin = fin;
    frame.opcode = opcode;
    frame.masked = masked;
    frame.payload.assign(reinterpret_cast<const std::uint8_t*>(payloadStart),
                         reinterpret_cast<const std::uint8_t*>(payloadStart + payloadSize));

    if (masked) {
        for (std::size_t i = 0; i < frame.payload.size(); ++i) {
            frame.payload[i] ^= maskKey[i % maskKey.size()];
        }
    }

    m_offset += offset + payloadSize;
    m_frames.push_back(std::move(frame));
    return true;
}

void WebSocketFrameParser::setError(std::string message)
{
    m_error = std::move(message);
}

bool Utf8Validator::feed(const std::uint8_t* data, std::size_t size)
{
    for (std::size_t i = 0; i < size && m_valid; ++i) {
        const std::uint8_t byte = data[i];
        if (m_remaining > 0) {
            if (byte < m_lower || byte > m_upper) {
                m_valid = false;
                break;
            }
            m_lower = 0x80;
            m_upper = 0xBF;
            --m_remaining;
            continue;
        }

        if (byte <= 0x7F) {
            continue;
        }
        if (byte >= 0xC2 && byte <= 0xDF) {
            m_remaining = 1;
        } else if (byte >= 0xE0 && byte <= 0xEF) {
            m_remaining = 2;
            if (byte == 0xE0) {
                m_lower = 0xA0;  // reject overlong 3-byte forms
            } else if (byte == 0xED) {
                m_upper = 0x9F;  // reject UTF-16 surrogates U+D800..U+DFFF
            }
        } else if (byte >= 0xF0 && byte <= 0xF4) {
            m_remaining = 3;
            if (byte == 0xF0) {
                m_lower = 0x90;  // reject overlong 4-byte forms
            } else if (byte == 0xF4) {
                m_upper = 0x8F;  // reject code points above U+10FFFF
            }
        } else {
            m_valid = false;  // 0x80..0xC1 lead bytes and 0xF5..0xFF
        }
    }
    return m_valid;
}

void Utf8Validator::reset()
{
    m_valid = true;
    m_remaining = 0;
    m_lower = 0x80;
    m_upper = 0xBF;
}

bool isValidUtf8(const std::uint8_t* data, std::size_t size)
{
    Utf8Validator validator;
    return validator.feed(data, size) && validator.isComplete();
}

bool isValidCloseCode(std::uint16_t code)
{
    if (code >= 1000 && code <= 1003) {
        return true;
    }
    if (code >= 1007 && code <= 1014) {
        return true;
    }
    return code >= 3000 && code <= 4999;
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
