#pragma once

#include "crawler/application/ports/persistence/ProducedArtifactRegistry.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的 producer 索引入口；只登记身份与实测参考，不发布正式文件。
class MySqlProducedArtifactRegistry final : public application::ports::ProducedArtifactRegistry {
public:
    // 复用组合根连接池，事务中不调用文件、网络或外部取消回调。
    explicit MySqlProducedArtifactRegistry(std::shared_ptr<MySqlConnectionPool> pool);
    // 父 MRC 当前认领与两项产物同事务核对；冲突时整组回滚。
    application::result::ServiceResult<std::vector<application::model::ResourceIdentity>> record(const application::model::ResourceTransferJob &parent, const application::model::LyricArtifactBundle &bundle, std::uint64_t source_size, const application::model::ResourceTransferContext &context) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 共享连接池，不共享事务状态
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
