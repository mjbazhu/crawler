#pragma once

#include "crawler/application/ports/persistence/SingerSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"

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
        std::string transaction_begin = "sql/mysql/transactions/transaction_begin.sql";                                         // 开启事务 SQL
        std::string transaction_commit = "sql/mysql/transactions/transaction_commit.sql";                                       // 提交事务 SQL
        std::string transaction_rollback = "sql/mysql/transactions/transaction_rollback.sql";                                   // 回滚事务 SQL
        std::string update_singer_source_profile = "sql/mysql/singer_source_refs/update_migu_singer_source_profile_counts.sql"; // 更新歌手来源概览 SQL
        std::string update_singer_name_pinyin = "sql/mysql/singer_source_refs/update_migu_singer_source_name_pinyin.sql";       // 回填歌手来源拼音 SQL
    };

    // 保存连接池和默认 SQL 模板。
    explicit MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池和指定 SQL 模板，便于测试或部署时替换模板路径。
    MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    // 同步 port 入口：内部桥接到 MySQL 协程写入并阻塞等待结果。
    bool saveBatch(const std::vector<application::model::SingerSourceMetadata> &singers) override;

    // 同步 port 入口：更新平台声明的歌曲和专辑总数。
    bool updateProfileCounts(const application::model::SingerSourceProfile &profile) override;

    // 同步 port 入口：批量回填当前为空的歌手来源拼音。
    bool updateNamePinyinBatch(const std::vector<application::model::SingerSourceNamePinyinPatch> &patches) override;

    // 协程式批量保存歌手来源，统一事务先写实体再写关系。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<application::model::SingerSourceMetadata> singers);

    // 协程式更新歌手来源概览总数。
    boost::asio::awaitable<bool> coUpdateProfileCounts(application::model::SingerSourceProfile profile);

    // 协程式批量回填歌手来源拼音。
    boost::asio::awaitable<bool> coUpdateNamePinyinBatch(std::vector<application::model::SingerSourceNamePinyinPatch> patches);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池

    MySqlMetadataBatchWriter writer_;       // 统一事务写入器
    std::vector<std::string> begin_sql_;    // 开启事务语句
    std::vector<std::string> commit_sql_;   // 提交事务语句
    std::vector<std::string> rollback_sql_; // 回滚事务语句
    std::string update_singer_profile_sql_; // 歌手来源概览更新语句
    std::string update_name_pinyin_sql_;    // 歌手来源拼音回填语句
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
