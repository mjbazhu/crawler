#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include <optional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// URL 解析结果，仅支持当前爬虫需要的 http/https。
struct ParsedUrl {
    std::string scheme; // 协议名
    std::string host;   // 主机名
    std::string port;   // 端口
    std::string target; // path + query
    bool tls = false;   // 是否使用 TLS
};

// 判断客户端是否启用 HTTP 代理。
bool proxyEnabled(const AsioHttpClient::Options &options);

// 解析 http/https URL；不支持其它 scheme。
std::optional<ParsedUrl> parseUrl(std::string_view raw_url);

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
