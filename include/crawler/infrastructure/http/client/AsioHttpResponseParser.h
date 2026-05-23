#pragma once

#include "crawler/infrastructure/http/client/HttpClient.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

inline constexpr std::size_t kMaxHeaderBytes = 64 * 1024; // 响应头最大字节数

// 查找 HTTP 响应头结束位置，返回 body 起始偏移。
std::size_t findHeaderEnd(const std::string &raw_response);

// 从 HTTP 状态行解析状态码。
int parseStatusCode(std::string_view headers_part);

// 解析完整 HTTP 响应，包含 chunked body 解码和大小限制。
std::optional<HttpResponse> parseHttpResponse(const std::string &raw_response, std::size_t max_response_bytes);

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
