#pragma once

#include "crawler/application/ports/persistence/SongRepository.h"
#include "crawler/domain/music/model/Song.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlCrudRepository.h"

#include <boost/mysql/row_view.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 歌曲仓储，使用 Boost.MySQL 协程接口完成真正异步写入。
class MySqlSongRepository : public MySqlCrudRepository<domain::Song, std::uint64_t>, public application::ports::SongRepository {
public:
    // 歌曲仓储所需的外置 SQL 文件路径。
    struct SqlPaths {
        std::string upsert = "sql/mysql/catalog/song_upsert.sql";             // 插入或更新歌曲 SQL
        std::string delete_by_id = "sql/mysql/catalog/song_delete_by_id.sql"; // 按本地 songs.id 删除 SQL
        std::string find_by_id = "sql/mysql/catalog/song_find_by_id.sql";     // 按本地 songs.id 查询 SQL
        std::string find_all = "sql/mysql/catalog/song_find_all.sql";         // 分页查询 SQL
    };

    // 保存连接池和 SQL 路径，构造阶段加载 SQL 文件以尽早暴露配置错误。
    explicit MySqlSongRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    // 同步 port 入口：内部桥接到 MySQL 协程写入并阻塞等待结果。
    bool saveBatch(const std::vector<domain::Song> &songs) override;

    // 协程式批量保存歌曲，内部使用单连接事务和 prepared statement。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<domain::Song> songs);

    // 协程式新增或覆盖歌曲。
    boost::asio::awaitable<bool> coCreate(const domain::Song &entity) override;

    // 协程式更新歌曲，当前与 upsert 语义一致。
    boost::asio::awaitable<bool> coUpdate(const domain::Song &entity) override;

    // 协程式按本地 songs.id 删除歌曲。
    boost::asio::awaitable<bool> coDeleteById(const std::uint64_t &id) override;

    // 协程式按本地 songs.id 查询歌曲。
    boost::asio::awaitable<std::optional<domain::Song>> coFindById(const std::uint64_t &id) override;

    // 协程式分页查询歌曲。
    boost::asio::awaitable<std::vector<domain::Song>> coFindAll(std::size_t limit, std::size_t offset) override;

private:
    // 把 MySQL 行映射为领域歌曲模型。
    static domain::Song mapRow(boost::mysql::row_view row);

    std::string upsert_sql_;   // 插入或更新歌曲 SQL 内容
    std::string delete_sql_;   // 删除歌曲 SQL 内容
    std::string find_sql_;     // 按 ID 查询歌曲 SQL 内容
    std::string find_all_sql_; // 分页查询歌曲 SQL 内容
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
