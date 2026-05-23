#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <algorithm>
#include <cctype>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

namespace {

// 转换为小写，供 scheme 和 header 名称比较使用。
std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

bool proxyEnabled(const AsioHttpClient::Options &options)
{
    return options.proxy.has_value() && options.proxy->enabled();
}

std::optional<ParsedUrl> parseUrl(std::string_view raw_url)
{
    const auto scheme_pos = raw_url.find("://");
    if (scheme_pos == std::string_view::npos) {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.scheme = toLower(std::string(raw_url.substr(0, scheme_pos)));
    parsed.tls = parsed.scheme == "https";
    if (parsed.scheme != "http" && parsed.scheme != "https") {
        return std::nullopt;
    }

    std::string_view rest = raw_url.substr(scheme_pos + 3);
    const auto target_pos = rest.find_first_of("/?#");
    std::string_view authority = target_pos == std::string_view::npos ? rest : rest.substr(0, target_pos);
    parsed.target = target_pos == std::string_view::npos ? "/" : std::string(rest.substr(target_pos));
    if (!parsed.target.empty() && parsed.target.front() == '?') {
        parsed.target.insert(parsed.target.begin(), '/');
    }

    if (authority.empty()) {
        return std::nullopt;
    }

    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        parsed.host = std::string(authority.substr(1, close - 1));
        if (close + 1 < authority.size() && authority[close + 1] == ':') {
            parsed.port = std::string(authority.substr(close + 2));
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos && authority.find(':') == colon) {
            parsed.host = std::string(authority.substr(0, colon));
            parsed.port = std::string(authority.substr(colon + 1));
        } else {
            parsed.host = std::string(authority);
        }
    }

    if (parsed.host.empty()) {
        return std::nullopt;
    }
    if (parsed.port.empty()) {
        parsed.port = parsed.tls ? "443" : "80";
    }
    if (parsed.target.empty()) {
        parsed.target = "/";
    }
    return parsed;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
