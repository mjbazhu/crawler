#pragma once

#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的实时准入查询；只读元信息，不初始化 schema 或访问资源字节。
class MySqlResourceTransferAccess final : public application::ports::ResourceTransferAccess {
public:
    // 注入已初始化连接池。
    explicit MySqlResourceTransferAccess(std::shared_ptr<MySqlConnectionPool> pool);
    // 读取资源与指定目标策略，local 不作为 cloud_provider。
    application::result::ServiceResult<application::ports::ResourceTransferAdmission> inspect(application::model::ResourceIdentity identity, application::model::ResourceDestination destination) override;
    // 所有目标均退出 maintenance、活动目标已就绪后才接收来源写入。
    application::result::ServiceResult<bool> writesAllowed() override;
    // 只检查指定目标，不能以另一活动目标代替 local 的可执行性。
    application::result::ServiceResult<bool> destinationReady(application::model::ResourceDestination destination) override;

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 共享的线程安全连接池
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
