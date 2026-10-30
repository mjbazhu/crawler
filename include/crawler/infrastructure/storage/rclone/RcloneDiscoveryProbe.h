#pragma once
#include "crawler/application/ports/storage/ResourceDiscoveryProbe.h"
#include "crawler/infrastructure/storage/rclone/RcloneConfig.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include <functional>
#include <memory>
namespace orchestrator::modules::crawler::infrastructure::storage::rclone {
// 身份核对由组合根注入，实例只共享不可变配置及线程安全HTTP客户端。
std::unique_ptr<application::ports::ResourceDiscoveryProbe> makeRcloneDiscoveryProbe(RcloneConfig config, std::shared_ptr<http::client::HttpClient> client, std::function<application::result::ServiceResult<std::string>(const application::model::ResourceTransferContext &)> identity);
} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
