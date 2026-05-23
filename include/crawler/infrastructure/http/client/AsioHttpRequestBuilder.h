#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// 构造 HTTPS 代理 CONNECT 请求。
std::string buildConnectRequest(const ParsedUrl &url, const AsioHttpClient::ProxyOptions &proxy);

// 构造完整 HTTP/1.1 请求报文。
std::string buildRequest(
    std::string_view method,
    const ParsedUrl &url,
    std::string_view body,
    const std::vector<std::string> &raw_headers,
    const AsioHttpClient::ProxyOptions *proxy
);

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
