#pragma once

#include "crawler/application/ports/persistence/SongSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 歌曲来源仓储，使用外置 SQL 模板写入歌曲、平台来源、音质参数和资源任务。
class MySqlSongSourceRepository : public application::ports::SongSourceRepository {
public:
    // 歌曲来源仓储所需的外置 SQL 文件路径。
    struct SqlPaths {
        std::string transaction_begin = "sql/mysql/common/transaction_begin.sql";               // 开启事务 SQL
        std::string transaction_commit = "sql/mysql/common/transaction_commit.sql";             // 提交事务 SQL
        std::string transaction_rollback = "sql/mysql/common/transaction_rollback.sql";         // 回滚事务 SQL
        std::string set_song_item_vars = "sql/mysql/05_set_migu_song_item_vars.sql";            // 绑定歌曲来源变量 SQL
        std::string insert_song_item = "sql/mysql/05_insert_migu_song_item.sql";                // 插入歌曲来源核心数据 SQL
        std::string set_audio_format_vars = "sql/mysql/06_set_migu_song_audio_format_vars.sql"; // 绑定音质参数变量 SQL
        std::string insert_audio_format = "sql/mysql/06_insert_migu_song_audio_format.sql";     // 插入音质参数和资源任务 SQL
        std::string set_artist_ref_vars = "sql/mysql/32_set_migu_song_artist_ref_vars.sql";     // 绑定歌曲歌手关系变量 SQL
        std::string insert_artist_ref = "sql/mysql/32_insert_migu_song_artist_ref.sql";          // 插入歌曲歌手关系 SQL
        std::string set_album_song_ref_vars = "sql/mysql/33_set_migu_album_song_ref_vars.sql";  // 绑定专辑歌曲关系变量 SQL
        std::string insert_album_song_ref = "sql/mysql/33_insert_migu_album_song_ref.sql";       // 插入专辑歌曲关系 SQL
        std::string insert_audio_placeholder = "sql/mysql/34_insert_migu_song_audio_placeholder.sql"; // 插入缺省音频资源任务 SQL
    };

    // 保存连接池和 SQL 模板，构造阶段加载 SQL 文件以尽早暴露配置错误。
    explicit MySqlSongSourceRepository(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池和指定 SQL 模板，便于测试或部署时替换模板路径。
    MySqlSongSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    // 同步 port 入口：内部桥接到 MySQL 协程写入并阻塞等待结果。
    bool saveBatch(const std::vector<application::model::SongSourceMetadata> &songs) override;

    // 协程式批量保存歌曲来源，单批次使用同一事务。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<application::model::SongSourceMetadata> songs);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池

    std::vector<std::string> begin_sql_;        // 开启事务语句
    std::vector<std::string> commit_sql_;       // 提交事务语句
    std::vector<std::string> rollback_sql_;     // 回滚事务语句
    std::string set_song_vars_sql_;             // 歌曲来源变量绑定语句
    std::vector<std::string> insert_song_sql_;  // 歌曲来源核心入库语句
    std::string set_audio_vars_sql_;            // 音质参数变量绑定语句
    std::vector<std::string> insert_audio_sql_; // 音质参数入库语句
    std::string set_artist_vars_sql_;           // 歌曲歌手关系变量绑定语句
    std::vector<std::string> insert_artist_sql_; // 歌曲歌手关系入库语句
    std::string set_album_song_vars_sql_;       // 专辑歌曲关系变量绑定语句
    std::vector<std::string> insert_album_song_sql_; // 专辑歌曲关系入库语句
    std::vector<std::string> insert_audio_placeholder_sql_; // 缺省音频资源任务入库语句
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
