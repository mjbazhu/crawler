#pragma once

#include "crawler/application/ports/persistence/SingerRepository.h"
#include "crawler/domain/music/model/Singer.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlCrudRepository.h"

#include <boost/mysql/row_view.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 歌手仓储，使用 Boost.MySQL 协程接口完成真正异步写入。
class MySqlSingerRepository : public MySqlCrudRepository<domain::Singer, std::int64_t>, public application::ports::SingerRepository {
public:
    // 歌手仓储所需的外置 SQL 文件路径。
    struct SqlPaths {
        std::string upsert = "sql/mysql/singers/upsert.sql";             // 插入或更新歌手 SQL
        std::string delete_by_id = "sql/mysql/singers/delete_by_id.sql"; // 按 singer_id 删除 SQL
        std::string find_by_id = "sql/mysql/singers/find_by_id.sql";     // 按 singer_id 查询 SQL
        std::string find_all = "sql/mysql/singers/find_all.sql";         // 分页查询 SQL
    };

    // 保存连接池和 SQL 路径，构造阶段加载 SQL 文件以尽早暴露配置错误。
    explicit MySqlSingerRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    // 同步 port 入口：内部桥接到 MySQL 协程写入并阻塞等待结果。
    bool saveBatch(const std::vector<domain::Singer> &singers) override;

    // 协程式批量保存歌手，内部使用单连接事务和 prepared statement。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<domain::Singer> singers);

    // 协程式新增或覆盖歌手。
    boost::asio::awaitable<bool> coCreate(const domain::Singer &entity) override;

    // 协程式更新歌手，当前与 upsert 语义一致。
    boost::asio::awaitable<bool> coUpdate(const domain::Singer &entity) override;

    // 协程式按本地 singers.id 删除歌手。
    boost::asio::awaitable<bool> coDeleteById(const std::int64_t &id) override;

    // 协程式按本地 singers.id 查询歌手。
    boost::asio::awaitable<std::optional<domain::Singer>> coFindById(const std::int64_t &id) override;

    // 协程式分页查询歌手。
    boost::asio::awaitable<std::vector<domain::Singer>> coFindAll(std::size_t limit, std::size_t offset) override;

private:
    // 把 MySQL 行映射为领域歌手模型。
    static domain::Singer mapRow(boost::mysql::row_view row);

    std::string upsert_sql_;   // 插入或更新歌手 SQL 内容
    std::string delete_sql_;   // 删除歌手 SQL 内容
    std::string find_sql_;     // 按 ID 查询歌手 SQL 内容
    std::string find_all_sql_; // 分页查询歌手 SQL 内容
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
