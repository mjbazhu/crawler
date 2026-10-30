#pragma once

#include "crawler/infrastructure/app/ResourceTargetVerifier.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::app::target_verification {
// 固定布局、实际账号/根的结构化摘要；不包含凭据和配置别名。
std::string fingerprint(const nlohmann::json &identity);
// 所有失败均清空指纹，不能误用上次或人工输入的 hash 放行。
ResourceTargetVerification unavailable(application::model::ResourceDestination destination, std::string adapter, std::string code);
// 标准 WebDAV 只核验挂载，不冒称能读取其后端网盘账号。
ResourceTargetVerification rcloneTarget(const ServerApplication::Config &config, const ResourceCloudTarget &target, const application::model::ResourceTransferContext &context, std::shared_ptr<http::client::HttpClient> client);
// 原生 Drive 以官方账号和实际目录身份核验；独立身份客户端仅供内部测试注入。
ResourceTargetVerification driveTarget(const ServerApplication::Config &config, const ResourceCloudTarget &target, const application::model::ResourceTransferContext &context, std::shared_ptr<http::client::HttpClient> rc_client, std::shared_ptr<http::client::HttpClient> identity_client = {});
} // namespace orchestrator::modules::crawler::infrastructure::app::target_verification
