#pragma once

#include "crawler/application/ports/persistence/AlbumSourceWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSqlPaths.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 专辑来源写入 adapter，只负责保存 album source 入库所需数据。
class MySqlAlbumSourceWriter : public application::ports::AlbumSourceWriter {
public:
    // 保存连接池并加载默认 SQL 模板。
    explicit MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池并加载指定 SQL 模板，便于测试或部署替换。
    MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool, MySqlAlbumSourceSqlPaths paths);

    // 同步 port 入口：内部桥接到 MySQL 协程写入并阻塞等待结果。
    bool saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums) override;

    // 协程式批量保存专辑来源，单批次使用同一事务。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<application::model::AlbumSourceMetadata> albums);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池

    std::vector<std::string> begin_sql_;        // 开启事务语句
    std::vector<std::string> commit_sql_;       // 提交事务语句
    std::vector<std::string> rollback_sql_;     // 回滚事务语句
    std::string set_album_vars_sql_;            // 专辑来源变量绑定语句
    std::vector<std::string> insert_album_sql_; // 专辑来源核心入库语句
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
