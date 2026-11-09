#pragma once

#include <SNFCore/ByteArray.h>
#include <SNFCore/Span.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace snf::websocket::detail {

enum class OpCode : std::uint8_t {
    Continuation = 0x0,
    Text         = 0x1,
    Binary       = 0x2,
    Close        = 0x8,
    Ping         = 0x9,
    Pong         = 0xA,
};

struct WebSocketFrame {
    bool fin = true;
    OpCode opcode = OpCode::Text;
    bool masked = false;
    std::vector<std::uint8_t> payload;
};

class WebSocketFrameParser
{
public:
    void feed(const std::uint8_t* data, std::size_t size);
    void feed(const std::vector<std::uint8_t>& data);
    /** @brief Overload accepting a non-owning byte span. */
    void feed(Span<const std::byte> data);

    bool hasFrame() const;
    WebSocketFrame nextFrame();

    bool hasError() const;
    const std::string& error() const;
    void reset();

private:
    bool tryParseOne();
    void setError(std::string message);

private:
    ByteArray::Storage m_buffer;
    std::size_t m_offset = 0;  // start of unparsed data in m_buffer
    std::deque<WebSocketFrame> m_frames;
    std::string m_error;
};

/**
 * @brief Incremental UTF-8 validator (RFC 3629).
 *
 * Rejects overlong encodings, UTF-16 surrogates and code points above
 * U+10FFFF as soon as the offending byte is seen, so a fragmented text
 * message can fail fast. A code point may be split across `feed()` calls.
 */
class Utf8Validator
{
public:
    /** @brief Validates more bytes. Returns false once the stream is invalid. */
    bool feed(const std::uint8_t* data, std::size_t size);
    bool feed(const std::vector<std::uint8_t>& data) { return feed(data.data(), data.size()); }

    /** @brief True when the stream is valid and does not end mid code point. */
    bool isComplete() const { return m_valid && m_remaining == 0; }
    bool isValid() const { return m_valid; }
    void reset();

private:
    bool m_valid = true;
    std::uint8_t m_remaining = 0;  // continuation bytes still expected
    std::uint8_t m_lower = 0x80;   // allowed range for the next continuation byte
    std::uint8_t m_upper = 0xBF;
};

/** @brief Returns true if @p data is a complete, valid UTF-8 sequence. */
bool isValidUtf8(const std::uint8_t* data, std::size_t size);

/** @brief Close status codes used when this endpoint fails the connection. */
namespace CloseCode {
constexpr std::uint16_t Normal = 1000;
constexpr std::uint16_t ProtocolError = 1002;
constexpr std::uint16_t InvalidPayload = 1007;
constexpr std::uint16_t MessageTooBig = 1009;
}  // namespace CloseCode

/**
 * @brief Returns true if @p code may appear in a Close frame on the wire.
 *
 * Accepts the RFC 6455 / IANA registered codes (1000-1003, 1007-1014) and
 * the library/application ranges 3000-4999. 1004-1006 and 1015 are reserved
 * and must never be sent.
 */
bool isValidCloseCode(std::uint16_t code);

std::vector<std::uint8_t> encodeFrame(OpCode opcode,
                                      const std::vector<std::uint8_t>& payload,
                                      bool maskPayload);

}  // namespace snf::websocket::detail
