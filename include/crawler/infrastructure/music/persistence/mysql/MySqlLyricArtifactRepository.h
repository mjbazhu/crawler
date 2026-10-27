#pragma once

#include "crawler/application/ports/persistence/LyricArtifactRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 派生产物索引 adapter；可跨 blocking worker 使用，不可在池自身 I/O 线程同步调用。
class MySqlLyricArtifactRepository final : public application::ports::LyricArtifactRepository {
public:
    // 复用进程 MySQL 连接池，仅加载本表 SQL 模板。
    explicit MySqlLyricArtifactRepository(std::shared_ptr<MySqlConnectionPool> pool);
    // 同事务登记 JSON/LRC，并核对来源当前校验值。
    application::result::ServiceResult<bool> record(const application::model::LyricArtifactBundle &bundle) override;
    // 失败只写派生表；相同来源和版本的已成功产物不会被降级。
    application::result::ServiceResult<bool> recordFailure(const application::model::LyricProcessingRequest &request, std::string_view version, int schema_version, std::string_view code) override;

private:
    // 按值持有协程参数，避免异步 SQL 引用调用方临时数据。
    boost::asio::awaitable<std::optional<application::result::ServiceResult<bool>>> coRecord(application::model::LyricArtifactBundle bundle, std::string failure);
    // 基础设施边界阻塞等待，不向 application 暴露 Asio。
    application::result::ServiceResult<bool> wait(application::model::LyricArtifactBundle bundle, std::string failure);

    std::shared_ptr<MySqlConnectionPool> pool_; // 共享线程安全连接池
    std::string select_source_sql_;             // 来源代次校验模板
    std::string upsert_sql_;                    // 产物索引模板
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
