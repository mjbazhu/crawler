#pragma once
#include "crawler/application/ports/persistence/ResourceDiscoveryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <memory>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的逐页发现仓储，跨中断/实例保持游标和认领围栏。
class MySqlResourceDiscovery final : public application::ports::ResourceDiscoveryRepository {
public:
    // 复用池，不自动迁移或连接存储。
    explicit MySqlResourceDiscovery(std::shared_ptr<MySqlConnectionPool> pool);
    // 同ID同范围重放，拒绝目标活动冲突。
    application::result::ServiceResult<application::model::ResourceDiscoveryRun> start(application::model::ResourceDiscoveryDefinition definition) override;
    // 只读任务定义及已提交进度。
    application::result::ServiceResult<application::model::ResourceDiscoveryRun> find(std::string id) override;
    // 认领下一页，过期时推进代次。
    application::result::ServiceResult<std::optional<application::model::ResourceDiscoveryRun>> claim(std::string id) override;
    // 检查租约与目标版本是否仍然有效。
    application::result::ServiceResult<bool> current(const application::model::ResourceDiscoveryRun &lease) override;
    // 原子提交整页观察、计数及游标。
    application::result::ServiceResult<bool> commit(const application::model::ResourceDiscoveryRun &lease, const application::model::LegacyResourceImportPage &page, std::vector<application::model::ResourceDiscoveryObservation> observations) override;
    // 暂停、恢复或取消，保留检查点。
    application::result::ServiceResult<application::model::ResourceDiscoveryRun> control(std::string id, std::string action) override;
    // 故障时仅暂停当前认领者。
    application::result::ServiceResult<bool> release(const application::model::ResourceDiscoveryRun &lease, std::string code) override;
    // 按固定上界读取已提交观察。
    application::result::ServiceResult<std::vector<application::model::ResourceDiscoveryObservation>> observations(std::string id, std::uint64_t after, std::uint64_t upper, std::size_t limit) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全池
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
