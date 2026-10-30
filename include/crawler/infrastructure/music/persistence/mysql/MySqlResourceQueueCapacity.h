#pragma once
#include "crawler/application/model/ResourceQueueCapacity.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <memory>
#include <vector>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的持久限额管理；不初始化schema、修改任务或访问存储。
class MySqlResourceQueueCapacity {
public:
    // 共享已有池。
    explicit MySqlResourceQueueCapacity(std::shared_ptr<MySqlConnectionPool> pool);
    // 同一快照返回三端限额和有界占用。
    application::result::ServiceResult<std::vector<application::model::ResourceQueueCapacity>> status();
    // 乐观版本更新；不能降到已占用数量以下，审计与配置同事务。
    application::result::ServiceResult<application::model::ResourceQueueCapacity> configure(application::model::ResourceDestination destination, std::uint64_t version, std::uint64_t transfers, std::uint64_t producers);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全池
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
