#pragma once

#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/infrastructure/app/ResourceTransferFactory.h"

#include <filesystem>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::app {

// 只返回脱敏指纹与固定错误，不携带账号、原始命令输出或认证。
struct ResourceTargetVerification {
    application::model::ResourceTransferTargetProfile profile; // 失败时 available=false 且没有可用指纹
    std::string result_code;                                   // 可诊断的固定结果码
    std::string identity_scope;                                // filesystem_object/provider_account/webdav_mount，声明不升级为底层核验
};

// 只读现存根目录及操作系统文件身份；缺失时不创建目录或假定目标。
ResourceTargetVerification verifyLocalResourceTarget(const std::filesystem::path &root, const application::model::ResourceTransferContext &context);
// 显式只读核验，不在工厂构造中执行；无法获得实际身份时保持不可用。
ResourceTargetVerification verifyCloudResourceTarget(const ServerApplication::Config &config, const ResourceCloudTarget &target, const application::model::ResourceTransferContext &context, std::shared_ptr<http::client::HttpClient> client = {}, execution::process::CommandRunner runner = {});

} // namespace orchestrator::modules::crawler::infrastructure::app
