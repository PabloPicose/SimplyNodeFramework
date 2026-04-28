#include "SNFWebSocket/WebSocketHandshake.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace snf::websocket::detail {

namespace {

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool containsToken(const std::string& value, const std::string& expectedToken)
{
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (toLower(trim(token)) == expectedToken) {
            return true;
        }
    }
    return false;
}

std::string base64Encode(const std::uint8_t* data, std::size_t size)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((size + 2) / 3) * 4);

    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t octetA = data[i];
        const std::uint32_t octetB = i + 1 < size ? data[i + 1] : 0;
        const std::uint32_t octetC = i + 2 < size ? data[i + 2] : 0;
        const std::uint32_t triple = (octetA << 16u) | (octetB << 8u) | octetC;

        output.push_back(alphabet[(triple >> 18u) & 0x3Fu]);
        output.push_back(alphabet[(triple >> 12u) & 0x3Fu]);
        output.push_back(i + 1 < size ? alphabet[(triple >> 6u) & 0x3Fu] : '=');
        output.push_back(i + 2 < size ? alphabet[triple & 0x3Fu] : '=');
    }

    return output;
}

int base64Value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

bool base64Decode(const std::string& input, std::vector<std::uint8_t>& output)
{
    output.clear();

    if (input.empty() || (input.size() % 4) != 0) {
        return false;
    }

    std::size_t padding = 0;
    if (!input.empty() && input[input.size() - 1] == '=') {
        ++padding;
    }
    if (input.size() > 1 && input[input.size() - 2] == '=') {
        ++padding;
    }

    for (std::size_t i = 0; i < input.size(); i += 4) {
        std::array<int, 4> values{};
        for (std::size_t j = 0; j < values.size(); ++j) {
            const char c = input[i + j];
            if (c == '=') {
                if (i + j < input.size() - padding) {
                    return false;
                }
                values[j] = 0;
            } else {
                values[j] = base64Value(c);
                if (values[j] < 0) {
                    return false;
                }
            }
        }

        const std::uint32_t triple =
            (static_cast<std::uint32_t>(values[0]) << 18u) |
            (static_cast<std::uint32_t>(values[1]) << 12u) |
            (static_cast<std::uint32_t>(values[2]) << 6u) |
            static_cast<std::uint32_t>(values[3]);

        output.push_back(static_cast<std::uint8_t>((triple >> 16u) & 0xFFu));
        if (input[i + 2] != '=') {
            output.push_back(static_cast<std::uint8_t>((triple >> 8u) & 0xFFu));
        }
        if (input[i + 3] != '=') {
            output.push_back(static_cast<std::uint8_t>(triple & 0xFFu));
        }
    }

    return true;
}

bool isValidClientKey(const std::string& key)
{
    std::vector<std::uint8_t> decoded;
    return base64Decode(key, decoded) && decoded.size() == 16;
}

class Sha1
{
public:
    void update(const std::string& data)
    {
        update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    }

    void update(const std::uint8_t* data, std::size_t size)
    {
        m_totalBytes += size;
        m_buffer.insert(m_buffer.end(), data, data + size);

        while (m_buffer.size() >= 64) {
            processBlock(m_buffer.data());
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + 64);
        }
    }

    std::array<std::uint8_t, 20> final()
    {
        const std::uint64_t totalBits = static_cast<std::uint64_t>(m_totalBytes) * 8u;

        m_buffer.push_back(0x80u);
        while ((m_buffer.size() % 64) != 56) {
            m_buffer.push_back(0x00u);
        }

        for (int shift = 56; shift >= 0; shift -= 8) {
            m_buffer.push_back(static_cast<std::uint8_t>((totalBits >> static_cast<unsigned>(shift)) & 0xFFu));
        }

        for (std::size_t offset = 0; offset < m_buffer.size(); offset += 64) {
            processBlock(m_buffer.data() + offset);
        }

        std::array<std::uint8_t, 20> digest{};
        writeWord(digest.data(), m_h0);
        writeWord(digest.data() + 4, m_h1);
        writeWord(digest.data() + 8, m_h2);
        writeWord(digest.data() + 12, m_h3);
        writeWord(digest.data() + 16, m_h4);
        return digest;
    }

private:
    static std::uint32_t leftRotate(std::uint32_t value, unsigned shift)
    {
        return (value << shift) | (value >> (32u - shift));
    }

    static void writeWord(std::uint8_t* output, std::uint32_t value)
    {
        output[0] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
        output[1] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
        output[2] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
        output[3] = static_cast<std::uint8_t>(value & 0xFFu);
    }

    static std::uint32_t readWord(const std::uint8_t* input)
    {
        return (static_cast<std::uint32_t>(input[0]) << 24u) |
               (static_cast<std::uint32_t>(input[1]) << 16u) |
               (static_cast<std::uint32_t>(input[2]) << 8u) |
               static_cast<std::uint32_t>(input[3]);
    }

    void processBlock(const std::uint8_t* block)
    {
        std::array<std::uint32_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = readWord(block + i * 4);
        }
        for (std::size_t i = 16; i < 80; ++i) {
            w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = m_h0;
        std::uint32_t b = m_h1;
        std::uint32_t c = m_h2;
        std::uint32_t d = m_h3;
        std::uint32_t e = m_h4;

        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }

            const std::uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = leftRotate(b, 30);
            b = a;
            a = temp;
        }

        m_h0 += a;
        m_h1 += b;
        m_h2 += c;
        m_h3 += d;
        m_h4 += e;
    }

private:
    std::uint32_t m_h0 = 0x67452301u;
    std::uint32_t m_h1 = 0xEFCDAB89u;
    std::uint32_t m_h2 = 0x98BADCFEu;
    std::uint32_t m_h3 = 0x10325476u;
    std::uint32_t m_h4 = 0xC3D2E1F0u;
    std::uint64_t m_totalBytes = 0;
    std::vector<std::uint8_t> m_buffer;
};

std::string headerValue(const HttpMessage& message, const std::string& key)
{
    const auto it = message.headers.find(toLower(key));
    if (it == message.headers.end()) {
        return {};
    }
    return it->second;
}

}  // namespace

std::string createClientKey()
{
    std::array<std::uint8_t, 16> bytes{};
    static thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> distribution(0, 255);
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(distribution(generator));
    }
    return base64Encode(bytes.data(), bytes.size());
}

std::string computeAcceptKey(const std::string& clientKey)
{
    Sha1 sha1;
    sha1.update(clientKey + kWebSocketGuid);
    const auto digest = sha1.final();
    return base64Encode(digest.data(), digest.size());
}

std::string buildClientHandshakeRequest(const std::string& host,
                                        std::uint16_t port,
                                        const std::string& path,
                                        const std::string& clientKey)
{
    const std::string requestPath = path.empty() ? "/" : path;
    std::ostringstream stream;
    stream << "GET " << requestPath << " HTTP/1.1\r\n"
           << "Host: " << host << ":" << port << "\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Key: " << clientKey << "\r\n"
           << "Sec-WebSocket-Version: 13\r\n"
           << "\r\n";
    return stream.str();
}

std::string buildServerHandshakeResponse(const std::string& clientKey)
{
    std::ostringstream stream;
    stream << "HTTP/1.1 101 Switching Protocols\r\n"
           << "Upgrade: websocket\r\n"
           << "Connection: Upgrade\r\n"
           << "Sec-WebSocket-Accept: " << computeAcceptKey(clientKey) << "\r\n"
           << "\r\n";
    return stream.str();
}

bool parseHttpMessage(const std::string& raw, HttpMessage& message, std::string& errorMessage)
{
    message = {};
    errorMessage.clear();

    std::istringstream stream(raw);
    if (!std::getline(stream, message.startLine)) {
        errorMessage = "Missing HTTP start line";
        return false;
    }
    message.startLine = trim(message.startLine);
    if (message.startLine.empty()) {
        errorMessage = "Empty HTTP start line";
        return false;
    }

    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) {
            break;
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            errorMessage = "Invalid HTTP header line";
            return false;
        }

        message.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    return true;
}

bool validateServerHandshakeResponse(const std::string& rawResponse,
                                     const std::string& expectedClientKey,
                                     std::string& errorMessage)
{
    HttpMessage message;
    if (!parseHttpMessage(rawResponse, message, errorMessage)) {
        return false;
    }

    std::istringstream startLine(message.startLine);
    std::string protocol;
    int statusCode = 0;
    if (!(startLine >> protocol >> statusCode) || protocol.rfind("HTTP/", 0) != 0 || statusCode != 101) {
        errorMessage = "WebSocket server did not return HTTP 101";
        return false;
    }

    if (toLower(headerValue(message, "Upgrade")) != "websocket") {
        errorMessage = "WebSocket server response missing Upgrade: websocket";
        return false;
    }

    if (!containsToken(headerValue(message, "Connection"), "upgrade")) {
        errorMessage = "WebSocket server response missing Connection: Upgrade";
        return false;
    }

    if (headerValue(message, "Sec-WebSocket-Accept") != computeAcceptKey(expectedClientKey)) {
        errorMessage = "WebSocket server response has invalid Sec-WebSocket-Accept";
        return false;
    }

    return true;
}

bool validateClientHandshakeRequest(const std::string& rawRequest,
                                    std::string& clientKey,
                                    std::string& errorMessage)
{
    clientKey.clear();

    HttpMessage message;
    if (!parseHttpMessage(rawRequest, message, errorMessage)) {
        return false;
    }

    std::istringstream startLine(message.startLine);
    std::string method;
    std::string path;
    std::string protocol;
    if (!(startLine >> method >> path >> protocol) || method != "GET" || protocol.rfind("HTTP/", 0) != 0) {
        errorMessage = "Invalid WebSocket upgrade request line";
        return false;
    }

    if (toLower(headerValue(message, "Upgrade")) != "websocket") {
        errorMessage = "WebSocket upgrade request missing Upgrade: websocket";
        return false;
    }

    if (!containsToken(headerValue(message, "Connection"), "upgrade")) {
        errorMessage = "WebSocket upgrade request missing Connection: Upgrade";
        return false;
    }

    if (headerValue(message, "Sec-WebSocket-Version") != "13") {
        errorMessage = "Unsupported WebSocket version";
        return false;
    }

    clientKey = headerValue(message, "Sec-WebSocket-Key");
    if (clientKey.empty()) {
        errorMessage = "WebSocket upgrade request missing Sec-WebSocket-Key";
        return false;
    }
    if (!isValidClientKey(clientKey)) {
        errorMessage = "WebSocket upgrade request has invalid Sec-WebSocket-Key";
        return false;
    }

    return true;
}

}  // namespace snf::websocket::detail
