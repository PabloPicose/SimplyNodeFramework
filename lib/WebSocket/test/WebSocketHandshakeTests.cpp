#include <gtest/gtest.h>

#include <string>

#include "SNFWebSocket/WebSocketHandshake.h"

using namespace snf::websocket::detail;

TEST(WebSocketHandshakeTest, ComputesAcceptKeyFromRfcExample)
{
    EXPECT_EQ(computeAcceptKey("dGhlIHNhbXBsZSBub25jZQ=="),
              "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocketHandshakeTest, ValidatesClientRequest)
{
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost:8765\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    std::string key;
    std::string error;
    EXPECT_TRUE(validateClientHandshakeRequest(request, key, error)) << error;
    EXPECT_EQ(key, "dGhlIHNhbXBsZSBub25jZQ==");
}

TEST(WebSocketHandshakeTest, BuildsClientRequestWithNormalizedPathAndIpv6Host)
{
    const std::string request = buildClientHandshakeRequest(
        "2001:db8::1",
        8765,
        "echo",
        "dGhlIHNhbXBsZSBub25jZQ==");

    EXPECT_NE(request.find("GET /echo HTTP/1.1\r\n"), std::string::npos);
    EXPECT_NE(request.find("Host: [2001:db8::1]:8765\r\n"), std::string::npos);
    EXPECT_NE(request.find("Upgrade: websocket\r\n"), std::string::npos);
}

TEST(WebSocketHandshakeTest, RejectsInvalidClientRequest)
{
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost:8765\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: abc\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    std::string key;
    std::string error;
    EXPECT_FALSE(validateClientHandshakeRequest(request, key, error));
    EXPECT_FALSE(error.empty());
}

TEST(WebSocketHandshakeTest, RejectsInvalidClientKey)
{
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost:8765\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: abc\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    std::string key;
    std::string error;
    EXPECT_FALSE(validateClientHandshakeRequest(request, key, error));
    EXPECT_EQ(error, "WebSocket upgrade request has invalid Sec-WebSocket-Key");
}

TEST(WebSocketHandshakeTest, BuildsAndValidatesServerResponse)
{
    const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    const std::string response = buildServerHandshakeResponse(key);

    std::string error;
    EXPECT_TRUE(validateServerHandshakeResponse(response, key, error)) << error;
}

TEST(WebSocketHandshakeTest, RejectsServerResponseWithWrongAccept)
{
    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: wrong\r\n"
        "\r\n";

    std::string error;
    EXPECT_FALSE(validateServerHandshakeResponse(response, "dGhlIHNhbXBsZSBub25jZQ==", error));
}
