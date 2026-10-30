#pragma once

#include "crawler/application/model/LegacyResourceImport.h"
#include "crawler/application/ports/persistence/LegacyResourceImportRepository.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的历史页导入 adapter；所有变更共用同一事务，未知提交不伪装回滚。
class MySqlLegacyResourceImport final : public application::ports::LegacyResourceImportRepository {
public:
    // 注入连接池，不初始化 schema 或访问文件/网盘。
    explicit MySqlLegacyResourceImport(std::shared_ptr<MySqlConnectionPool> pool);
    // 读取固定范围的只读预览页。
    application::result::ServiceResult<application::model::LegacyResourceImportPage> preview(application::model::ResourceOwner owner, std::uint64_t after_id, std::uint64_t high_water_id, std::size_t limit) override;
    // 原子导入已确认的双端历史记录。
    application::result::ServiceResult<application::model::LegacyResourceImportResult> importPage(application::model::LegacyResourceImportPlan plan, application::model::LegacyResourceImportPage page) override;
    // 只读核对清单、数据库及双端页游标。
    application::result::ServiceResult<application::model::LegacyResourceImportProgress> progress(application::model::LegacyResourceImportPlan plan, application::model::LegacyResourceImportPage scope) override;
    // 同事务控制双端声明导入；暂停/取消不撤销已提交历史策略。
    application::result::ServiceResult<application::model::LegacyResourceImportProgress> control(application::model::LegacyResourceImportPlan plan, application::model::LegacyResourceImportPage scope, std::string action);
    // 在维护状态注册核实过的目标指纹；变更目标时推进修订并撤销就绪标记。
    application::result::ServiceResult<std::uint64_t> configureTarget(application::model::ResourceDestination destination, std::uint64_t expected_revision, std::string fingerprint);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 共享线程安全连接池
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
