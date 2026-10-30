#pragma once

#include "crawler/application/ports/persistence/ResourceProducerTaskRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的独立生成队列，状态与文件传输预算分开。
class MySqlResourceProducerTasks final : public application::ports::ResourceProducerTaskRepository {
public:
    // 复用既有池，不初始化数据库或启动生成器。
    explicit MySqlResourceProducerTasks(std::shared_ptr<MySqlConnectionPool> pool);
    // 只有冻结处理版本、实际目标及输入能力匹配才认领。
    application::result::ServiceResult<std::optional<application::model::ResourceProducerTask>> claim(application::model::ResourceDestination destination, std::string fingerprint, std::string adapter, std::string processor_version, int schema_version, bool key_available) override;
    // producer 自己的认领/取消边界独立于父传输是否已经结束。
    application::result::ServiceResult<bool> shouldStop(const application::model::ResourceProducerTask &task) override;
    // 产物参考、逐端子操作与成功状态在一个事务中提交。
    application::result::ServiceResult<std::string> complete(const application::model::ResourceProducerTask &task, const application::model::LyricArtifactBundle &bundle, std::uint64_t source_size) override;
    // 固定预算内重试，永久错误可查询。
    application::result::ServiceResult<bool> fail(const application::model::ResourceProducerTask &task, application::model::ResourceTransferJobFailure failure) override;
    // 单页最多 200 条，过期认领推进代次后才能继续。
    application::result::ServiceResult<int> recover(application::model::ResourceDestination destination, int stale_seconds) override;
    // 显式重试不改变冻结来源、目标或处理规则。
    application::result::ServiceResult<bool> retry(std::uint64_t producer_id) override;
    // 仅查询本父操作至多 600 条生成计划。
    application::result::ServiceResult<std::vector<application::model::ResourceProducerTask>> list(std::string parent_operation_id) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 操作独立事务，共享线程安全连接池
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
