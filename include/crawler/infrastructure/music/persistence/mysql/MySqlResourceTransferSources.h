#pragma once
#include "crawler/application/ports/persistence/ResourceTransferSources.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <memory>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的四类来源描述读取，不执行正文 I/O。
class MySqlResourceTransferSources final : public application::ports::ResourceTransferSources {
public:
    // 共享已初始化连接池。
    explicit MySqlResourceTransferSources(std::shared_ptr<MySqlConnectionPool> pool);
    // 比较冻结版本，返回已登记描述。
    application::result::ServiceResult<application::model::ResourceTransferSourceInput> describe(const application::model::ResourceTransferJob &job) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 按连接隔离读取
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
