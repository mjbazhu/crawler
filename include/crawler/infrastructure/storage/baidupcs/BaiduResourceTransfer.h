#pragma once

#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/infrastructure/execution/process/ProcessRunner.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoConfig.h"

#include <cstdint>
#include <memory>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {
namespace transfer {
struct ReadbackQuota;
}

// 线程安全的文件上传/回读策略，每个操作独占子进程与验证目录。
class BaiduResourceTransfer final : public application::ports::ResourceTransferDestination {
public:
    // 临时根只用于本端回读，不依赖正式 local；构造不建目录或探测网盘。
    BaiduResourceTransfer(BaiduPcsGoConfig config, execution::process::CommandRunner runner = {}, std::uint64_t temporary_bytes = std::uint64_t {8} * 1024 * 1024 * 1024);
    // 只读取明确对象并分片核对 SHA-256，不重新获取来源。
    application::result::ServiceResult<application::model::ResourceAttemptInspection> inspect(const application::model::ResourceTransferJob &job, const application::model::ResourceTransferContext &context) override;
    // 验证快照、固定对象身份；共享配置不支持逐任务限速时明确拒绝。
    application::result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> prepare(const application::model::ResourceTransferJob &job, std::shared_ptr<const application::model::ResourceContentSnapshot> snapshot, const application::model::ResourceTransferContext &context) override;

private:
    BaiduPcsGoConfig config_;                        // 固定后端与目标根，不修改客户端账号/全局设置
    execution::process::CommandRunner runner_;       // 每次调用独立管理进程及其子进程
    std::shared_ptr<transfer::ReadbackQuota> quota_; // 本目标全部回读的临时空间准入
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
