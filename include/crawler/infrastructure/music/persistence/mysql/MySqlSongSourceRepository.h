#pragma once

#include "crawler/application/ports/persistence/SongSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"

#include <boost/asio/awaitable.hpp>
#include <memory>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 线程安全的兼容入口；实体拆分后委托统一事务 writer，不维护跨表 SQL。
class MySqlSongSourceRepository : public application::ports::SongSourceRepository {
public:
    // 关联连接池并加载各表 SQL。
    explicit MySqlSongSourceRepository(std::shared_ptr<MySqlConnectionPool> pool);

    // 同步写入入口，整批实体和关系具有同一事务边界。
    bool saveBatch(const std::vector<application::model::SongSourceMetadata> &songs) override;

    // 协程写入入口，失败返回 false 并回滚整个批次。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<application::model::SongSourceMetadata> songs);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池
    MySqlMetadataBatchWriter writer_;           // 不可变表级 SQL 与事务编排
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
