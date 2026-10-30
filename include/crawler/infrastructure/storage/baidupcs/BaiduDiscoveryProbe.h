#pragma once
#include "crawler/application/ports/storage/ResourceDiscoveryProbe.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoConfig.h"
#include "crawler/infrastructure/execution/process/ProcessRunner.h"
#include <functional>
#include <memory>
namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {
// 原生百度仅查询meta，不下载、上传或将服务端分片摘要当内容摘要。
std::unique_ptr<application::ports::ResourceDiscoveryProbe> makeBaiduDiscoveryProbe(BaiduPcsGoConfig config, execution::process::CommandRunner runner, std::function<application::result::ServiceResult<std::string>(const application::model::ResourceTransferContext &)> identity);
} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
