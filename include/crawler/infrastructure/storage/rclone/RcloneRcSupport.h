#pragma once

#include "crawler/infrastructure/storage/rclone/RcloneConfig.h"

#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone::detail {

// 共用 RC Basic Auth；认证值只进入请求头。
std::vector<std::string> rcHeaders(const RcloneConfig &config, std::string_view content_type = "application/json");
// 将 fs 和 remote 安全编码到 RC 查询参数。
std::string queryEncode(std::string_view value);

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone::detail
