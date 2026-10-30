#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <string>
#include <cstdint>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// 构造 HTTPS 代理 CONNECT 请求。
std::string buildConnectRequest(const ParsedUrl &url, const AsioHttpClient::ProxyOptions &proxy);

// 请求体可由流式发送者提供，长度由客户端统一托管。
std::string buildRequestHead(std::string_view method, const ParsedUrl &url, std::uint64_t body_size, const std::vector<std::string> &raw_headers, const AsioHttpClient::ProxyOptions *proxy);

// 构造完整 HTTP/1.1 请求报文。
std::string buildRequest(
    std::string_view method,
    const ParsedUrl &url,
    std::string_view body,
    const std::vector<std::string> &raw_headers,
    const AsioHttpClient::ProxyOptions *proxy
);

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
