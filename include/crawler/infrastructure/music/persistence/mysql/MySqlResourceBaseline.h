#pragma once

#include "crawler/application/model/ResourceBaseline.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的维护仓储；全程不请求源站、读取文件或操作网盘。
class MySqlResourceBaseline {
public:
    // 注入既有池，不初始化或迁移数据库。
    explicit MySqlResourceBaseline(std::shared_ptr<MySqlConnectionPool> pool);
    // 只读重核四类清单、审计、策略、目标与未决占用，产生可审阅摘要。
    application::result::ServiceResult<application::model::ResourceBaselineReview> review(application::model::ResourceBaselinePlan plan);
    // 原子记录封存及排除策略；摘要变化时拒绝，不开启传输。
    application::result::ServiceResult<application::model::ResourceBaselineReview> seal(application::model::ResourceBaselinePlan plan, std::string reviewed_digest);
    // 再次重核封存内容，选定目标 active，其他目标 paused，原子放行。
    application::result::ServiceResult<application::model::ResourceBaselineReview> activate(std::string baseline_id, std::string reviewed_digest, std::vector<application::model::ResourceDestination> destinations, bool writers_stopped);

    // 明确停写后原子退回全局维护；保留回执/意图，原基线恢复sealed并记录审计。
    application::result::ServiceResult<application::model::ResourceBaselineReview> maintenance(std::string baseline_id, std::string reviewed_digest, bool writers_stopped);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 共享连接池
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
