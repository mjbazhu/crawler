#pragma once

#include "crawler/application/model/CloudFile.h"
#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/ports/persistence/MetadataCoverCloudQueue.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"
#include "crawler/application/ports/storage/CloudFileStore.h"
#include "crawler/application/ports/storage/MetadataCoverReader.h"
#include "crawler/application/result/ServiceResult.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 线程安全的共用云端用例；百度与 Google 使用相同的认领、重试和回写。
class MetadataCoverCloudWorkflow {
public:
    // 注入独立云端队列、文件读取和所选存储后端。
    MetadataCoverCloudWorkflow(std::shared_ptr<ports::MetadataCoverCloudQueue> queue, std::shared_ptr<ports::MetadataCoverReader> reader, std::shared_ptr<ports::CloudFileStore> store, int timeout_ms, std::shared_ptr<ports::ResourceTransferAccess> access = nullptr, std::optional<model::ResourceDestination> destination = std::nullopt);
    // 处理已成功的本地文件，失败保留文件和本地下载状态。
    model::MetadataCoverItem process(model::MetadataCoverRecord target, int retries, const std::string &batch_id, std::function<bool()> cancelled = {});
    // 比较已确认回执、文件和当前目标配置。
    bool matches(const model::MetadataCoverRecord &target) const;
    // 将目标后端与前缀加入待补传筛选。
    void candidates(model::MetadataCoverQuery &query) const;
    // 恢复云端超时认领。
    result::ServiceResult<int> recover(const model::MetadataCoverQuery &query, int stale_seconds);

private:
    std::shared_ptr<ports::MetadataCoverCloudQueue> queue_; // 独立云端队列
    std::shared_ptr<ports::MetadataCoverReader> reader_;    // 本地内容核验
    std::shared_ptr<ports::CloudFileStore> store_;          // 配置选定的后端
    int timeout_ms_;                                        // 单项上传与核验超时
    std::shared_ptr<ports::ResourceTransferAccess> access_; // 内容读取前的持久策略
    std::optional<model::ResourceDestination> destination_; // 固定的兼容平台，不从 rclone 或 remote 别名猜测
};

} // namespace orchestrator::modules::crawler::application::use_cases
