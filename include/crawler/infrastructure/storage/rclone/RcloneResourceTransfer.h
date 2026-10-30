#pragma once

#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/model/ResourceTransferJobs.h"

#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/rclone/RcloneConfig.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone {

// 线程安全的文件快照传输；RC 调用与正文均有独立取消/超时，无全量音频聚合。
class RcloneResourceTransfer final : public application::ports::ResourceTransferDestination {
public:
    // 只验证配置，不探测 remote，不打开正式 local 根。
    RcloneResourceTransfer(std::shared_ptr<http::client::HttpClient> client, RcloneConfig config, application::model::ResourceDestination provider);
    // 已有意图只读核对；未决写入缺少停止证明时，暂时缺失仍为 Unknown。
    application::result::ServiceResult<application::model::ResourceAttemptInspection> inspect(const application::model::ResourceTransferJob &job, const application::model::ResourceTransferContext &context) override;
    // 验证私有文件并冻结独占对象；不在 prepare 中上传。
    application::result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> prepare(const application::model::ResourceTransferJob &job, std::shared_ptr<const application::model::ResourceContentSnapshot> snapshot, const application::model::ResourceTransferContext &context) override;

private:
    application::model::ResourceDestination provider_; // 明确的平台归属，不根据 remote 名称或工具推断
    std::shared_ptr<http::client::HttpClient> client_; // 线程安全、专用于本目标的客户端
    RcloneConfig config_;                              // 不可变 RC 与命名 remote 配置
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
