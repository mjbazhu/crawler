#pragma once

#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 外部音乐平台返回的原始响应，供 application 编排后续解析。
struct ProviderResponse {
    int status_code = 0;      // HTTP 状态码
    std::vector<char> body;   // 原始响应体
    std::string content_type; // 响应 Content-Type
};

} // namespace orchestrator::modules::crawler::application::model
