#pragma once

#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/ports/storage/CloudFileStore.h"
#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/execution/process/ProcessRunner.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "crawler/infrastructure/storage/rclone/RcloneConfig.h"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::app {

// 明确选择平台；临时根与正式 local 根分别提供，构造时不打开文件系统。
struct ResourceCloudTarget {
    application::model::ResourceDestination provider;                       // 只允许 Baidu/Gdrive
    std::filesystem::path staging_root;                                     // BaiduPCS 原生回读临时根；rclone 可空，不隐式借用 local
    std::uint64_t temporary_bytes = std::uint64_t {8} * 1024 * 1024 * 1024; // 本端回读预留上限
    std::string adapter;                                                    // 空值复用既有 resource_cloud_upload_backend，与 provider 分开
    std::optional<storage::rclone::RcloneConfig> rclone;                    // 空值复用既有 RC 配置；双端可各自覆盖
    std::shared_ptr<http::client::HttpClient> rclone_client;                // 可选逐端客户端；默认逐端创建，不共享执行器
};

// 一个目标的装配结果；配置错误保留为该目标的结果，不能静默漏掉。
struct ResourceCloudAssembly {
    application::model::ResourceDestination provider;                          // 目标平台
    std::shared_ptr<application::ports::CloudFileStore> store;                 // 可独立调用的适配器
    std::string result_code;                                                   // 固定、脱敏的单端错误
    std::shared_ptr<application::ports::ResourceTransferDestination> transfer; // 文件快照传输，与旧 bytes port 分开
    std::string transfer_result_code;                                          // 新策略装配失败不破坏旧封面端口
    std::string adapter;                                                       // 已解析工具，可用于冻结任务配置，不反推平台
};

// 工厂的独立目标集合；现有 legacy 入口选择固定一端，未来并行调度消费全部目标。
struct ResourceTransferAssembly {
    std::shared_ptr<storage::filesystem::FileStorage> local;                         // 禁用时为空，不创建正式根
    std::shared_ptr<application::ports::ResourceTransferDestination> local_transfer; // 真实流式发布/核对策略
    std::string local_result_code;                                                   // 本地单端装配结果
    std::vector<ResourceCloudAssembly> clouds;                                       // 包含成功与失败的所有已请求平台
    // 按平台查询，失败目标返回空但其错误仍保留在 clouds。
    std::shared_ptr<application::ports::CloudFileStore> cloud(application::model::ResourceDestination provider) const;
    // 统一取得 local 或某云端的文件传输策略，失败原因保留在对应装配项。
    std::shared_ptr<application::ports::ResourceTransferDestination> destination(application::model::ResourceDestination provider) const;
};

// 不可变配置快照工厂；线程安全，装配不执行下载、探测或上传。
class ResourceTransferFactory {
public:
    // 配置仅存于 infrastructure；不向 application/日志暴露凭据。
    explicit ResourceTransferFactory(ServerApplication::Config config);
    // 旧封面平台声明；baidupcs 可确定百度，rclone 未声明时返回空，不猜 Google。
    std::optional<application::model::ResourceDestination> legacyProvider() const;
    // 保留原通用云端 port 及其配置，尚无平台声明时也不丢弃原有 rclone 能力。
    std::shared_ptr<application::ports::CloudFileStore> legacyCloud(std::shared_ptr<http::client::HttpClient> rclone_client = {}, execution::process::CommandRunner runner = {}) const;
    // 一次构造本地与任意已选双端；某端错误不阻止其他端装配。
    ResourceTransferAssembly build(bool local_enabled, const std::vector<ResourceCloudTarget> &clouds, std::shared_ptr<http::client::HttpClient> rclone_client = {}, execution::process::CommandRunner runner = {}) const;

private:
    ServerApplication::Config config_; // 创建后不变的配置快照
};
} // namespace orchestrator::modules::crawler::infrastructure::app
