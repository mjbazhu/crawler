#pragma once

#include "crawler/application/ports/persistence/ResourceTransferQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的逐平台回执查询，不读取云端或修改资源。
class MySqlResourceTransferQueries final : public application::ports::ResourceTransferQueryRepository {
public:
    // 复用已有池，查询始终在有界等待中执行。
    explicit MySqlResourceTransferQueries(std::shared_ptr<MySqlConnectionPool> pool);
    // 兼容结构可读取，与 maintenance/parallel 无关。
    application::result::ServiceResult<bool> compatible() override;
    // 单条联表查询，不在目录页对每条资源发出额外 SQL。
    application::result::ServiceResult<application::model::ResourceCloudReceiptPage> receipts(application::model::ResourceCloudReceiptQuery query) override;

    // 单条SQL以请求身份驱动PK联表，避免逐文件查询。
    application::result::ServiceResult<application::model::ResourceStatusBatch> status(application::model::ResourceStatusQuery query) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
