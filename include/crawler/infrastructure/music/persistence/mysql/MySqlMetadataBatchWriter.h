#pragma once

#include "crawler/application/ports/persistence/MetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>

#include <map>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的元数据事务 adapter；SQL 只读共享，连接和主键映射属于单次批次。
class MySqlMetadataBatchWriter final : public application::ports::MetadataBatchWriter {
public:
    // 加载按表组织的 SQL，尽早发现部署缺少模板的问题。
    explicit MySqlMetadataBatchWriter(std::shared_ptr<MySqlConnectionPool> pool);

    // 同步桥接，保留明确失败类型及诊断说明。
    application::result::ServiceResult<application::model::MetadataWriteResult> saveBatch(const application::model::MetadataWriteBatch &batch) override;

    // 一个连接先写实体，再写关系和资源；失败整体回滚。
    boost::asio::awaitable<application::result::ServiceResult<application::model::MetadataWriteResult>> coSaveBatch(application::model::MetadataWriteBatch batch);

private:
    std::shared_ptr<MySqlConnectionPool> pool_;  // 线程安全连接池
    std::map<std::string, std::string> scripts_; // 构造后不变的表级 SQL
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
