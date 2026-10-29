#pragma once

#include "crawler/application/ports/storage/CloudFileStore.h"
#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/execution/process/ProcessRunner.h"
#include "crawler/infrastructure/http/client/HttpClient.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::app {

// 按同一开关和后端配置装配共用存储 port；关闭时不创建或调用 uploader。
std::shared_ptr<application::ports::CloudFileStore> makeCloudFileStore(const ServerApplication::Config &config, std::shared_ptr<http::client::HttpClient> rclone_client, execution::process::CommandRunner runner = {});

} // namespace orchestrator::modules::crawler::infrastructure::app
