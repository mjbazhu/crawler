#pragma once
#include "crawler/application/model/ResourceCloudCleanup.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <memory>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的删除前证据捕获与只读预演；没有远端执行端口。
class MySqlResourceCloudCleanup {
public:
    // 不初始化schema或连接网盘。
    explicit MySqlResourceCloudCleanup(std::shared_ptr<MySqlConnectionPool> pool);
    // 显式停写声明及全维护闸门，捕获/审计同事务，不删除资源。
    application::result::ServiceResult<application::model::ResourceCloudCleanupCapture> capture(std::vector<application::model::ResourceIdentity> resources, bool writers_stopped);
    // 单个快照事务核对引用/目标/意图，分页读取，不提供删除能力。
    application::result::ServiceResult<application::model::ResourceCloudCleanupPage> preview(std::optional<application::model::ResourceDestination> provider, std::uint64_t after, std::size_t limit);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 共享线程安全池
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
