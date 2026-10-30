#pragma once

#include "crawler/application/ports/persistence/ResourceTransferJobRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的持久任务仓储；网络和文件副作用始终在短事务之外执行。
class MySqlResourceTransferJobs final : public application::ports::ResourceTransferJobRepository {
public:
    // 共享已初始化的连接池，不自动放行维护闸门。
    explicit MySqlResourceTransferJobs(std::shared_ptr<MySqlConnectionPool> pool);
    // 原子登记父操作、各端任务和独占槽；幂等重放使用第一次冻结的配置。
    application::result::ServiceResult<application::model::ResourceTransferOperation> enqueue(application::model::ResourceTransferRequest request, std::vector<application::model::ResourceTransferTargetProfile> profiles) override;
    // 只读查询，终态和历史任务跨重启保留。
    application::result::ServiceResult<application::model::ResourceTransferOperation> find(std::string operation_id) override;
    // 独立认领一端，不等待其他端完成。
    application::result::ServiceResult<std::optional<application::model::ResourceTransferJob>> claim(application::model::ResourceDestination destination, std::string fingerprint, std::string adapter) override;
    // 取消、内容或目标版本变化均停止旧 worker。
    application::result::ServiceResult<bool> shouldStop(const application::model::ResourceTransferJob &job) override;
    // 副作用前登记独占尝试身份，用于崩溃后的核对。
    application::result::ServiceResult<bool> beginAttempt(const application::model::ResourceTransferJob &job, application::model::ResourceTransferAttempt attempt) override;
    // 仅当前任务、内容、来源及目标均匹配才确认对象。
    application::result::ServiceResult<bool> complete(const application::model::ResourceTransferJob &job, application::model::ResourceTransferReceipt receipt) override;
    // 暂态失败有限重试，未决副作用保留占用直到显式修复。
    application::result::ServiceResult<bool> fail(const application::model::ResourceTransferJob &job, application::model::ResourceTransferJobFailure failure) override;
    // 取消本操作及其一层 producer 子操作的选定目标；空集合取消全部，不影响其他父操作。
    application::result::ServiceResult<bool> cancel(std::string operation_id, std::vector<application::model::ResourceDestination> destinations) override;
    // 显式恢复指定目标的过期认领；有尝试记录的任务进入待核对。
    application::result::ServiceResult<int> recover(application::model::ResourceDestination destination, int stale_seconds) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池，操作按连接隔离
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
