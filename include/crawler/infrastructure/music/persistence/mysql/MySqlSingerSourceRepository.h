#pragma once

#include "crawler/application/ports/persistence/SingerSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 歌手来源仓储，使用外置 SQL 模板写入 singers 和 singer_source_refs。
class MySqlSingerSourceRepository : public application::ports::SingerSourceRepository {
public:
    // 歌手来源仓储所需的外置 SQL 文件路径。
    struct SqlPaths {
        std::string transaction_begin = "sql/mysql/common/transaction_begin.sql";            // 开启事务 SQL
        std::string transaction_commit = "sql/mysql/common/transaction_commit.sql";          // 提交事务 SQL
        std::string transaction_rollback = "sql/mysql/common/transaction_rollback.sql";      // 回滚事务 SQL
        std::string set_singer_source_vars = "sql/mysql/14_set_migu_singer_source_vars.sql"; // 绑定歌手来源变量 SQL
        std::string insert_singer_source = "sql/mysql/14_insert_migu_singer_source.sql";     // 插入歌手来源核心数据 SQL
    };

    // 保存连接池和默认 SQL 模板。
    explicit MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池和指定 SQL 模板，便于测试或部署时替换模板路径。
    MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    // 同步 port 入口：内部桥接到 MySQL 协程写入并阻塞等待结果。
    bool saveBatch(const std::vector<application::model::SingerSourceMetadata> &singers) override;

    // 协程式批量保存歌手来源，内部会分片事务写入以降低并发锁冲突。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<application::model::SingerSourceMetadata> singers);

private:
    // 保存一个小分片，内部独立事务和短锁，避免大批量并发写入死锁。
    boost::asio::awaitable<bool> coSaveChunk(
        const std::vector<application::model::SingerSourceMetadata> &singers,
        std::size_t first,
        std::size_t last,
        int max_attempts,
        std::string_view context
    );

    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池

    std::vector<std::string> begin_sql_;         // 开启事务语句
    std::vector<std::string> commit_sql_;        // 提交事务语句
    std::vector<std::string> rollback_sql_;      // 回滚事务语句
    std::string set_singer_vars_sql_;            // 歌手来源变量绑定语句
    std::vector<std::string> insert_singer_sql_; // 歌手来源核心入库语句
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
