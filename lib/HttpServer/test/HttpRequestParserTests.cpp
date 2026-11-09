#include "gtest/gtest.h"
#include "SNFHttpServer/HttpRequestParser.h"

using namespace snf;

class HttpRequestParserTest : public ::testing::Test
{
};

// ============================================================================
// Basic Request Parsing Tests
// ============================================================================

TEST_F(HttpRequestParserTest, ParseValidGetRequest)
{
    HttpRequestParser parser;
    std::string request = "GET /api/status HTTP/1.1\r\nHost: localhost\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());
    EXPECT_FALSE(parser.hasError());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.path, "/api/status");
    EXPECT_TRUE(req.body.empty());
}

TEST_F(HttpRequestParserTest, ParseValidPostRequest)
{
    HttpRequestParser parser;
    std::string body = "key=value&foo=bar";
    std::string request = "POST /api/command HTTP/1.1\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\n\r\n" + body;
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());
    EXPECT_FALSE(parser.hasError());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.path, "/api/command");
    EXPECT_EQ(req.body, body);
}

TEST_F(HttpRequestParserTest, ParseGetWithQueryString)
{
    HttpRequestParser parser;
    std::string request = "GET /api/search?q=test&limit=10 HTTP/1.1\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());
    EXPECT_FALSE(parser.hasError());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.path, "/api/search");
    EXPECT_EQ(req.getQueryParam("q"), "test");
    EXPECT_EQ(req.getQueryParam("limit"), "10");
}

TEST_F(HttpRequestParserTest, ParseHeadersAreCaseInsensitive)
{
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\nContent-Type: application/json\r\nHost: localhost\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.getHeader("content-type"), "application/json");
    EXPECT_EQ(req.getHeader("Content-Type"), "application/json");
    EXPECT_EQ(req.getHeader("CONTENT-TYPE"), "application/json");
}

TEST_F(HttpRequestParserTest, ParseMultipleHeaders)
{
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nUser-Agent: test\r\n"
                          "Accept: */*\r\nX-Custom-Header: custom-value\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.getHeader("Host"), "localhost");
    EXPECT_EQ(req.getHeader("User-Agent"), "test");
    EXPECT_EQ(req.getHeader("Accept"), "*/*");
    EXPECT_EQ(req.getHeader("X-Custom-Header"), "custom-value");
}

// ============================================================================
// Incremental Feeding Tests
// ============================================================================

TEST_F(HttpRequestParserTest, PartialRequestThenComplete)
{
    HttpRequestParser parser;

    // Feed request line first
    parser.feed("GET /test HTTP/1.1\r\n");
    EXPECT_FALSE(parser.isComplete());

    // Feed headers
    parser.feed("Host: localhost\r\n");
    EXPECT_FALSE(parser.isComplete());

    // Feed blank line to end headers
    parser.feed("\r\n");
    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.path, "/test");
}

TEST_F(HttpRequestParserTest, FeedByteByByte)
{
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\n\r\n";

    // Feed one character at a time
    for (char c : request)
    {
        parser.feed(reinterpret_cast<const uint8_t*>(&c), 1);
    }

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.path, "/");
}

TEST_F(HttpRequestParserTest, MultipleRequests)
{
    HttpRequestParser parser;

    // First request
    parser.feed("GET /first HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(parser.isComplete());
    HttpRequest req1 = parser.parse();
    EXPECT_EQ(req1.path, "/first");

    // Parser should be reset and ready for next request
    EXPECT_FALSE(parser.isComplete());

    // Second request
    parser.feed("POST /second HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(parser.isComplete());
    HttpRequest req2 = parser.parse();
    EXPECT_EQ(req2.method, "POST");
    EXPECT_EQ(req2.path, "/second");
}

// ============================================================================
// Content-Length Body Parsing
// ============================================================================

TEST_F(HttpRequestParserTest, PostWithContentLength)
{
    HttpRequestParser parser;
    std::string body = "This is the request body";
    std::string request = "POST /api/data HTTP/1.1\r\n"
                          "Content-Type: text/plain\r\n"
                          "Content-Length: " +
                          std::to_string(body.size()) + "\r\n\r\n" + body;
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.body, body);
    EXPECT_EQ(req.contentType(), "text/plain");
    EXPECT_EQ(req.contentLength(), body.size());
}

TEST_F(HttpRequestParserTest, LargeBodyWithContentLength)
{
    HttpRequestParser parser;
    std::string body(10000, 'x'); // 10KB body
    std::string request = "POST / HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) +
                          "\r\n\r\n" + body;
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.body, body);
}

TEST_F(HttpRequestParserTest, EmptyBodyWithContentLength)
{
    HttpRequestParser parser;
    std::string request = "POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_TRUE(req.body.empty());
}

// ============================================================================
// Chunked Transfer-Encoding
// ============================================================================

TEST_F(HttpRequestParserTest, ChunkedTransferEncoding)
{
    HttpRequestParser parser;
    std::string request = "POST / HTTP/1.1\r\n"
                          "Transfer-Encoding: chunked\r\n\r\n"
                          "5\r\nHello\r\n"
                          "6\r\n World\r\n"
                          "0\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.body, "Hello World");
}

// ============================================================================
// Error Cases
// ============================================================================

TEST_F(HttpRequestParserTest, InvalidRequestLineFormat)
{
    HttpRequestParser parser;
    std::string request = "INVALID REQUEST LINE WITHOUT METHOD\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.hasError());
    EXPECT_FALSE(parser.isComplete());
    EXPECT_FALSE(parser.error().empty());
}

TEST_F(HttpRequestParserTest, UnsupportedHttpVersion)
{
    HttpRequestParser parser;
    std::string request = "GET / HTTP/2.0\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.hasError());
}

TEST_F(HttpRequestParserTest, InvalidHeaderFormat)
{
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\nInvalidHeaderWithoutColon\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.hasError());
}

TEST_F(HttpRequestParserTest, InvalidChunkSize)
{
    HttpRequestParser parser;
    std::string request = "POST / HTTP/1.1\r\n"
                          "Transfer-Encoding: chunked\r\n\r\n"
                          "INVALID_HEX\r\ndata\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.hasError());
}

TEST_F(HttpRequestParserTest, ErrorStopsProcessing)
{
    HttpRequestParser parser;
    std::string request = "INVALID\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.hasError());

    // Feeding more data should not change state
    parser.feed("GET / HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(parser.hasError());
    EXPECT_FALSE(parser.isComplete());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(HttpRequestParserTest, RootPath)
{
    HttpRequestParser parser;
    parser.feed("GET / HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.path, "/");
}

TEST_F(HttpRequestParserTest, PathWithSpecialCharacters)
{
    HttpRequestParser parser;
    parser.feed("GET /api/users/123/details HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.path, "/api/users/123/details");
}

TEST_F(HttpRequestParserTest, QueryStringWithSpecialCharacters)
{
    HttpRequestParser parser;
    parser.feed("GET /search?q=hello+world&filter=active HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.getQueryParam("q"), "hello world");
    EXPECT_EQ(req.getQueryParam("filter"), "active");
}

// ============================================================================
// Query String Decoding (application/x-www-form-urlencoded)
// ============================================================================

namespace {

HttpRequest parseTarget(const std::string& target)
{
    HttpRequestParser parser;
    parser.feed("GET " + target + " HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(parser.isComplete());
    return parser.parse();
}

} // namespace

TEST_F(HttpRequestParserTest, QueryPercentEncodedValueIsDecoded)
{
    HttpRequest req = parseTarget("/api/frames?a=A%7C0%7C3");
    EXPECT_EQ(req.getQueryParam("a"), "A|0|3");
}

TEST_F(HttpRequestParserTest, QueryPlusAndPercent20AreSpaces)
{
    EXPECT_EQ(parseTarget("/x?a=EVA+12").getQueryParam("a"), "EVA 12");
    EXPECT_EQ(parseTarget("/x?a=EVA%2012").getQueryParam("a"), "EVA 12");
}

TEST_F(HttpRequestParserTest, QueryEncodedSeparatorsDoNotSplitTheParameter)
{
    HttpRequest req = parseTarget("/x?a=x%26y%3Dz");
    EXPECT_EQ(req.queryParams.size(), 1u);
    EXPECT_EQ(req.getQueryParam("a"), "x&y=z");
}

TEST_F(HttpRequestParserTest, QueryMalformedPercentIsLeftAsIs)
{
    EXPECT_EQ(parseTarget("/x?a=100%").getQueryParam("a"), "100%");
    EXPECT_EQ(parseTarget("/x?a=%zz").getQueryParam("a"), "%zz");
    EXPECT_EQ(parseTarget("/x?a=%4").getQueryParam("a"), "%4");
}

TEST_F(HttpRequestParserTest, QueryKeyIsDecoded)
{
    HttpRequest req = parseTarget("/x?%61=1");
    EXPECT_EQ(req.getQueryParam("a"), "1");
}

TEST_F(HttpRequestParserTest, QueryUtf8BytesAreDecodedVerbatim)
{
    EXPECT_EQ(parseTarget("/x?a=%c3%b1").getQueryParam("a"), "\xC3\xB1");
    EXPECT_EQ(parseTarget("/x?a=%C3%B1").getQueryParam("a"), "\xC3\xB1");
}

TEST_F(HttpRequestParserTest, QueryKeyWithoutValueIsDecoded)
{
    HttpRequest req = parseTarget("/x?a%20b");
    EXPECT_EQ(req.queryParams.count("a b"), 1u);
}

TEST_F(HttpRequestParserTest, QueryParameterWithoutValue)
{
    HttpRequestParser parser;
    parser.feed("GET /api?enabled&verbose HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.getQueryParam("enabled"), "");
    EXPECT_EQ(req.getQueryParam("verbose"), "");
}

TEST_F(HttpRequestParserTest, EmptyQueryString)
{
    HttpRequestParser parser;
    parser.feed("GET /path? HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.path, "/path");
}

TEST_F(HttpRequestParserTest, MethodNormalization)
{
    HttpRequestParser parser;
    parser.feed("post /api HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.method, "POST"); // Should be uppercase
}

TEST_F(HttpRequestParserTest, HeaderValueWithWhitespace)
{
    HttpRequestParser parser;
    parser.feed("GET / HTTP/1.1\r\nX-Custom:   value with spaces   \r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.getHeader("X-Custom"), "value with spaces");
}

TEST_F(HttpRequestParserTest, MultipleQueryParameters)
{
    HttpRequestParser parser;
    parser.feed("GET /?a=1&b=2&c=3&d=4 HTTP/1.1\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.getQueryParam("a"), "1");
    EXPECT_EQ(req.getQueryParam("b"), "2");
    EXPECT_EQ(req.getQueryParam("c"), "3");
    EXPECT_EQ(req.getQueryParam("d"), "4");
}

TEST_F(HttpRequestParserTest, GetRequestWithBody)
{
    // Some clients might send a body with GET (not standard, but should handle)
    HttpRequestParser parser;
    std::string body = "This is unusual";
    std::string request = "GET /path HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) +
                          "\r\n\r\n" + body;
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.body, body);
}

TEST_F(HttpRequestParserTest, ContentLengthZero)
{
    HttpRequestParser parser;
    parser.feed("POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_TRUE(req.body.empty());
}

TEST_F(HttpRequestParserTest, NoContentLengthAndNoBody)
{
    HttpRequestParser parser;
    parser.feed("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_TRUE(req.body.empty());
}

TEST_F(HttpRequestParserTest, InvalidContentLength)
{
    HttpRequestParser parser;
    parser.feed("POST / HTTP/1.1\r\nContent-Length: not_a_number\r\n\r\n");

    EXPECT_TRUE(parser.isComplete()); // Headers parsed, body expected size is 0
    HttpRequest req = parser.parse();
    EXPECT_EQ(req.contentLength(), 0);
}

TEST_F(HttpRequestParserTest, RequestWithLongPath)
{
    HttpRequestParser parser;
    std::string longPath = "/api" + std::string(1000, 'x');
    std::string request = "GET " + longPath + " HTTP/1.1\r\n\r\n";
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.path, longPath);
}

TEST_F(HttpRequestParserTest, PostWithJsonBody)
{
    HttpRequestParser parser;
    std::string body = R"({"name":"John","age":30})";
    std::string request = "POST /api/users HTTP/1.1\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: " +
                          std::to_string(body.size()) + "\r\n\r\n" + body;
    parser.feed(request);

    EXPECT_TRUE(parser.isComplete());

    HttpRequest req = parser.parse();
    EXPECT_EQ(req.body, body);
    EXPECT_EQ(req.contentType(), "application/json");
}

TEST_F(HttpRequestParserTest, DifferentHttpMethods)
{
    std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH"};

    for (const auto& method : methods)
    {
        HttpRequestParser parser;
        std::string request = method + " /path HTTP/1.1\r\n\r\n";
        parser.feed(request);

        EXPECT_TRUE(parser.isComplete()) << "Failed for method: " << method;

        HttpRequest req = parser.parse();
        EXPECT_EQ(req.method, method) << "Method not parsed correctly";
    }
}
