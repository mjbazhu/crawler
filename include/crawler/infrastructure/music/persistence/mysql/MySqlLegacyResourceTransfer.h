#pragma once

#include "crawler/application/ports/persistence/LegacyResourceTransferRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <atomic>
#include <memory>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 短事务保存旧协议投影，不复用旧下载writer。
class MySqlLegacyResourceTransfer final : public application::ports::LegacyResourceTransferRepository {
public:
    explicit MySqlLegacyResourceTransfer(std::shared_ptr<MySqlConnectionPool> pool);
    application::result::ServiceResult<application::model::LegacyResourceTransferBatch> start(application::model::LegacyResourceTransferRequest request) override;
    application::result::ServiceResult<std::optional<application::model::LegacyResourceTransferBatch>> find(std::string batch_id) override;
    application::result::ServiceResult<bool> cancel(std::string batch_id) override;
    application::result::ServiceResult<bool> pollNext() override;
    application::result::ServiceResult<application::model::MetadataCoverPage> listCovers(application::model::LegacyResourceTransferRequest request) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_;
    std::atomic<std::uint64_t> cursor_ {0}; // 观察调度游标不影响持久业务状态
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
