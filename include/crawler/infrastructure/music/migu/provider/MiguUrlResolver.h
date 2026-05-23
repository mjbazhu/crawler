#pragma once

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// Migu URL 归一化器，只负责把常见页面 URL 转换为可请求的资源 API URL。
class MiguUrlResolver {
public:
    // 返回归一化后的 URL；无法解析平台资源 ID 时返回 nullopt。
    static std::optional<std::string> resolve(std::string url);
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
