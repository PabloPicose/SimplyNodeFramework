#include "SNFHttpServer/HttpRequest.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace snf {

std::string HttpRequest::getHeader(const std::string& key) const
{
    // Case-insensitive search
    for (const auto& [k, v] : headers)
    {
        if (std::equal(k.begin(), k.end(), key.begin(), key.end(),
                       [](unsigned char a, unsigned char b) {
                           return std::tolower(a) == std::tolower(b);
                       }))
        {
            return v;
        }
    }
    return "";
}

std::string HttpRequest::getQueryParam(const std::string& key) const
{
    auto it = queryParams.find(key);
    if (it != queryParams.end())
        return it->second;
    return "";
}

std::string HttpRequest::contentType() const
{
    std::string ct = getHeader("Content-Type");
    if (ct.empty())
        return "text/plain";
    return ct;
}

std::size_t HttpRequest::contentLength() const
{
    std::string clStr = getHeader("Content-Length");
    if (clStr.empty())
        return 0;
    try
    {
        return std::stoul(clStr);
    }
    catch (...)
    {
        return 0;
    }
}

} // namespace snf
