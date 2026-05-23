#include "crawler/infrastructure/http/client/AsioHttpRequestBuilder.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

namespace {

// 请求头键值对，保留原始大小写以便发出请求。
struct HeaderLine {
    std::string name;  // Header 名称
    std::string value; // Header 值
};

// 转换为小写，供 header 名称比较使用。
std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// 去掉字符串两端空白。
std::string trim(std::string_view value)
{
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

// Basic 代理认证所需的 Base64 编码。
std::string base64Encode(std::string_view input)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        const unsigned char a = static_cast<unsigned char>(input[i++]);
        const unsigned char b = static_cast<unsigned char>(input[i++]);
        const unsigned char c = static_cast<unsigned char>(input[i++]);
        output.push_back(kAlphabet[a >> 2]);
        output.push_back(kAlphabet[((a & 0x03) << 4) | (b >> 4)]);
        output.push_back(kAlphabet[((b & 0x0f) << 2) | (c >> 6)]);
        output.push_back(kAlphabet[c & 0x3f]);
    }

    const std::size_t remaining = input.size() - i;
    if (remaining == 1) {
        const unsigned char a = static_cast<unsigned char>(input[i]);
        output.push_back(kAlphabet[a >> 2]);
        output.push_back(kAlphabet[(a & 0x03) << 4]);
        output.push_back('=');
        output.push_back('=');
    } else if (remaining == 2) {
        const unsigned char a = static_cast<unsigned char>(input[i]);
        const unsigned char b = static_cast<unsigned char>(input[i + 1]);
        output.push_back(kAlphabet[a >> 2]);
        output.push_back(kAlphabet[((a & 0x03) << 4) | (b >> 4)]);
        output.push_back(kAlphabet[(b & 0x0f) << 2]);
        output.push_back('=');
    }

    return output;
}

// 解析调用方传入的原始 header 行。
std::vector<HeaderLine> parseHeaders(const std::vector<std::string> &raw_headers)
{
    std::vector<HeaderLine> headers;
    headers.reserve(raw_headers.size());
    for (const auto &header : raw_headers) {
        const auto pos = header.find(':');
        if (pos != std::string::npos) {
            HeaderLine parsed;
            parsed.name = trim(std::string_view(header).substr(0, pos));
            parsed.value = trim(std::string_view(header).substr(pos + 1));
            if (!parsed.name.empty()) {
                headers.push_back(std::move(parsed));
            }
        }
    }
    return headers;
}

// 判断请求头中是否已有指定名称。
bool hasHeader(const std::vector<HeaderLine> &headers, std::string_view name)
{
    const std::string wanted = toLower(std::string(name));
    return std::any_of(headers.begin(), headers.end(), [&wanted](const HeaderLine &header) {
        return toLower(header.name) == wanted;
    });
}

// 判断是否是客户端统一托管的请求头。
bool isManagedHeader(std::string_view name)
{
    const std::string normalized = toLower(std::string(name));
    return normalized == "host" || normalized == "connection" || normalized == "content-length" || normalized == "accept-encoding";
}

// 构造 Host header 值，默认端口不追加。
std::string hostHeaderValue(const ParsedUrl &url)
{
    const bool default_port = (url.tls && url.port == "443") || (!url.tls && url.port == "80");
    if (default_port) {
        return url.host;
    }
    return url.host + ":" + url.port;
}

// HTTP 代理直连明文请求时使用 absolute-form target。
std::string absoluteProxyTarget(const ParsedUrl &url)
{
    return url.scheme + "://" + hostHeaderValue(url) + url.target;
}

// 构造 Proxy-Authorization 请求头。
std::optional<std::string> proxyAuthorizationHeader(const AsioHttpClient::ProxyOptions &proxy)
{
    if (proxy.username.empty() && proxy.password.empty()) {
        return std::nullopt;
    }
    return "Proxy-Authorization: Basic " + base64Encode(proxy.username + ":" + proxy.password);
}

} // namespace

std::string buildConnectRequest(const ParsedUrl &url, const AsioHttpClient::ProxyOptions &proxy)
{
    std::ostringstream request;
    const std::string authority = hostHeaderValue(url);
    request << "CONNECT " << authority << " HTTP/1.1\r\n";
    request << "Host: " << authority << "\r\n";
    request << "Proxy-Connection: Keep-Alive\r\n";
    request << "User-Agent: Crawler-AsioHttpClient/1.0\r\n";
    if (auto auth = proxyAuthorizationHeader(proxy)) {
        request << *auth << "\r\n";
    }
    request << "\r\n";
    return request.str();
}

std::string buildRequest(
    std::string_view method,
    const ParsedUrl &url,
    std::string_view body,
    const std::vector<std::string> &raw_headers,
    const AsioHttpClient::ProxyOptions *proxy
)
{
    const auto headers = parseHeaders(raw_headers);

    std::ostringstream request;
    const bool use_absolute_uri = proxy != nullptr && !url.tls;
    request << method << ' ' << (use_absolute_uri ? absoluteProxyTarget(url) : url.target) << " HTTP/1.1\r\n";
    request << "Host: " << hostHeaderValue(url) << "\r\n";
    request << "Connection: close\r\n";
    request << "Accept-Encoding: identity\r\n";
    if (proxy && use_absolute_uri) {
        if (auto auth = proxyAuthorizationHeader(*proxy)) {
            request << *auth << "\r\n";
        }
    }

    if (!hasHeader(headers, "Accept")) {
        request << "Accept: */*\r\n";
    }
    if (!hasHeader(headers, "User-Agent")) {
        request << "User-Agent: Crawler-AsioHttpClient/1.0\r\n";
    }

    for (const auto &header : headers) {
        if (!isManagedHeader(header.name)) {
            request << header.name << ": " << header.value << "\r\n";
        }
    }

    if (method == "POST") {
        request << "Content-Length: " << body.size() << "\r\n";
    }
    request << "\r\n";
    request << body;
    return request.str();
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
