#pragma once

#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 线程安全本地策略，四类归属共用现有流式会话与补偿日志。
class LocalResourceTransfer final : public application::ports::ResourceTransferDestination {
public:
    // 不创建正式根，只保存延迟写入的文件存储。
    explicit LocalResourceTransfer(std::shared_ptr<FileStorage> files);
    // 有界核对本次旧尝试；未知或不匹配不能当作明确不存在。
    application::result::ServiceResult<application::model::ResourceAttemptInspection> inspect(const application::model::ResourceTransferJob &job, const application::model::ResourceTransferContext &context) override;
    // 分片复制到 staging，返回发布前已固定的实际独占路径。
    application::result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> prepare(const application::model::ResourceTransferJob &job, std::shared_ptr<const application::model::ResourceContentSnapshot> snapshot, const application::model::ResourceTransferContext &context) override;

private:
    std::shared_ptr<FileStorage> files_; // 线程安全保存器，每次写入独占会话
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
