#pragma once
#include "crawler/application/model/ResourceHistoryAdoption.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <memory>
#include <string>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的单端历史维护适配器，不进行文件、网盘或内容I/O。
class MySqlResourceHistoryAdoption {
public:
    // 共享已初始化连接池，不运行DDL。
    explicit MySqlResourceHistoryAdoption(std::shared_ptr<MySqlConnectionPool> pool);
    // 固定清单同页重放，整页策略/审计/游标原子提交。
    application::result::ServiceResult<application::model::ResourceHistoryAdoptionProgress> apply(application::model::ResourceHistoryAdoptionPlan plan, application::model::ResourceHistoryAdoptionPage page);
    // 读取当前进度，不写策略。
    application::result::ServiceResult<application::model::ResourceHistoryAdoptionProgress> progress(application::model::ResourceHistoryAdoptionPlan plan);
    // 按明确控制代次暂停、恢复或取消，保留已提交策略。
    application::result::ServiceResult<application::model::ResourceHistoryAdoptionProgress> control(application::model::ResourceHistoryAdoptionPlan plan, std::string action);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全池，事务按连接隔离
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
