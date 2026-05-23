#include "crawler/infrastructure/music/migu/provider/MiguPacmtokenClient.h"

#include "crawler/infrastructure/music/migu/provider/MiguRequestFactory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

namespace {

constexpr const char *kCheckPacmtokenUrl = "https://c.musicapp.migu.cn/mgateway/api/checkPacMtoken";

// 当前 Unix 秒时间戳。
std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

// 转换为小写，供响应头名称比较使用。
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

// 从 Set-Cookie 头中提取 pacmtoken 值。
std::optional<std::string> extractPacmtokenFromCookie(std::string_view cookie)
{
    const std::size_t cookie_end = cookie.find(';');
    const std::string first_part = trim(cookie.substr(0, cookie_end));
    const std::size_t equal = first_part.find('=');
    if (equal == std::string::npos) {
        return std::nullopt;
    }

    std::string name = trim(std::string_view(first_part).substr(0, equal));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (name != "pacmtoken") {
        return std::nullopt;
    }
    return trim(std::string_view(first_part).substr(equal + 1));
}

// 从响应头列表中优先提取 pacmtoken 响应头，其次提取 Set-Cookie。
std::optional<std::string> extractPacmtokenFromHeaders(const std::vector<http::client::HttpHeader> &headers)
{
    for (const auto &header : headers) {
        if (toLower(header.name) == "pacmtoken") {
            return trim(header.value);
        }
    }
    for (const auto &header : headers) {
        if (toLower(header.name) == "set-cookie") {
            if (auto value = extractPacmtokenFromCookie(header.value); value && !value->empty()) {
                return value;
            }
        }
    }
    return std::nullopt;
}

// 将响应体限制为小字符串，仅用于解析上游错误码和说明。
std::string responseBodyText(const std::vector<char> &body)
{
    constexpr std::size_t kMaxBodyText = 4096;
    const std::size_t size = (std::min)(body.size(), kMaxBodyText);
    return std::string(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(size));
}

// 提取上游 JSON 中的 code/returnCode，用于判断 200 响应里的业务失败。
std::string bodyCode(const std::vector<char> &body)
{
    const auto parsed = nlohmann::json::parse(responseBodyText(body), nullptr, false);
    if (!parsed.is_object()) {
        return {};
    }
    for (const char *field : {"code", "returnCode", "resultCode"}) {
        if (parsed.contains(field) && parsed[field].is_string()) {
            return parsed[field].get<std::string>();
        }
        if (parsed.contains(field) && parsed[field].is_number_integer()) {
            return std::to_string(parsed[field].get<int>());
        }
    }
    return {};
}

// 提取上游 JSON 中的脱敏说明文本。
std::string bodyMessage(const std::vector<char> &body)
{
    const auto parsed = nlohmann::json::parse(responseBodyText(body), nullptr, false);
    if (!parsed.is_object()) {
        return {};
    }
    for (const char *field : {"message", "info", "msg", "error"}) {
        if (parsed.contains(field) && parsed[field].is_string()) {
            return parsed[field].get<std::string>();
        }
    }
    return {};
}

// 判断常见 Migu 成功编码。
bool isSuccessCode(const std::string &code)
{
    return code.empty() || code == "0" || code == "0000" || code == "000000" || code == "200";
}

// 构造失败结果。
application::model::MiguPacmtokenRefreshResult failure(std::string error_code, std::string message, int upstream_status = 0)
{
    application::model::MiguPacmtokenRefreshResult result;
    result.success = false;
    result.error_code = std::move(error_code);
    result.message = std::move(message);
    result.upstream_status = upstream_status;
    result.updated_at = nowSeconds();
    return result;
}

} // namespace

MiguPacmtokenClient::MiguPacmtokenClient(std::shared_ptr<http::client::HttpClient> client) : client_(std::move(client))
{}

application::model::MiguPacmtokenRefreshResult MiguPacmtokenClient::refreshPacmtoken(std::string pacmtoken)
{
    if (!client_) {
        return failure("not_configured", "migu pacmtoken http client is not configured");
    }

    auto headers = MiguRequestFactory::defaultHeaders();
    headers.push_back("pacmtoken: " + std::move(pacmtoken));
    auto response = client_->get(kCheckPacmtokenUrl, headers);
    if (!response) {
        return failure("network_failed", "migu checkPacMtoken request failed");
    }

    const std::string code = bodyCode(response->body);
    const std::string message = bodyMessage(response->body);
    if (response->status_code < 200 || response->status_code >= 300) {
        return failure("upstream_failed", message.empty() ? "migu checkPacMtoken returned non-success status" : message, response->status_code);
    }

    const auto refreshed_token = extractPacmtokenFromHeaders(response->headers);
    if (!isSuccessCode(code) && (!refreshed_token || refreshed_token->empty())) {
        return failure("upstream_rejected", message.empty() ? "migu rejected pacmtoken" : message, response->status_code);
    }

    application::model::MiguPacmtokenRefreshResult result;
    result.success = true;
    result.refreshed = refreshed_token && !refreshed_token->empty();
    result.pacmtoken = refreshed_token.value_or(std::string {});
    result.pacmtoken_length = result.pacmtoken.size();
    result.upstream_status = response->status_code;
    result.message = message.empty() ? "migu pacmtoken checked" : message;
    result.updated_at = nowSeconds();
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
