#pragma once

#include "crawler/application/ports/persistence/AlbumSourceWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSqlPaths.h"

#include <boost/asio/awaitable.hpp>
#include <memory>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的兼容入口；实体拆分后委托统一事务 writer，不维护跨表 SQL。
class MySqlAlbumSourceWriter : public application::ports::AlbumSourceWriter {
public:
    // 关联连接池并加载各表 SQL。
    explicit MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool);

    // 兼容原装配签名；表级模板统一由事务 writer 管理。
    MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool, MySqlAlbumSourceSqlPaths paths);

    // 同步写入入口，整批实体和关系具有同一事务边界。
    bool saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums) override;

    // 协程写入入口，失败返回 false 并回滚整个批次。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<application::model::AlbumSourceMetadata> albums);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池
    MySqlMetadataBatchWriter writer_;           // 不可变表级 SQL 与事务编排
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
