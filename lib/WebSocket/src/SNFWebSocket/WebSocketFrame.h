#pragma once

#include <cstdint>
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

    bool hasFrame() const;
    WebSocketFrame nextFrame();

    bool hasError() const;
    const std::string& error() const;
    void reset();

private:
    bool tryParseOne();
    void setError(std::string message);

private:
    std::vector<std::uint8_t> m_buffer;
    std::vector<WebSocketFrame> m_frames;
    std::string m_error;
};

std::vector<std::uint8_t> encodeFrame(OpCode opcode,
                                      const std::vector<std::uint8_t>& payload,
                                      bool maskPayload);

}  // namespace snf::websocket::detail
