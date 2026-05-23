#pragma once

#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/row_view.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 歌曲资源队列 adapter，负责资源任务认领和状态回写。
class MySqlSongResourceQueue : public application::ports::SongResourceQueue {
public:
    // SQL 文件路径集合。
    struct SqlPaths {
        std::string claim_select = "sql/mysql/resource/song_resource_claim_select.sql";                 // 主资源认领候选查询
        std::string claim_update = "sql/mysql/resource/song_resource_claim_update.sql";                 // 主资源认领状态更新
        std::string related_claim_update = "sql/mysql/resource/song_resource_related_claim_update.sql"; // 关联资源认领更新
        std::string related_find = "sql/mysql/resource/song_resource_related_find.sql";                 // 关联资源认领后查询
        std::string mark_succeeded = "sql/mysql/resource/song_resource_mark_succeeded.sql";             // 成功回写
        std::string mark_failed = "sql/mysql/resource/song_resource_mark_failed.sql";                   // 失败回写
        std::string mark_skipped = "sql/mysql/resource/song_resource_mark_skipped.sql";                 // 跳过回写
    };

    // 保存连接池并加载默认 SQL。
    explicit MySqlSongResourceQueue(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池并加载指定 SQL，便于测试替换。
    MySqlSongResourceQueue(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    std::vector<application::model::SongResourceQueueTarget> claimPending(application::model::SongResourceClaimQuery query) override;
    std::optional<application::model::SongResourceQueueTarget> claimRelated(application::model::SongResourceRelatedClaimRequest request) override;
    bool markSucceeded(application::model::SongResourceSuccessUpdate update) override;
    bool markFailed(application::model::SongResourceFailureUpdate update) override;
    bool markSkipped(application::model::SongResourceSkipUpdate update) override;

    boost::asio::awaitable<std::vector<application::model::SongResourceQueueTarget>> coClaimPending(application::model::SongResourceClaimQuery query);
    boost::asio::awaitable<std::optional<application::model::SongResourceQueueTarget>> coClaimRelated(application::model::SongResourceRelatedClaimRequest request);
    boost::asio::awaitable<bool> coMarkSucceeded(application::model::SongResourceSuccessUpdate update);
    boost::asio::awaitable<bool> coMarkFailed(application::model::SongResourceFailureUpdate update);
    boost::asio::awaitable<bool> coMarkSkipped(application::model::SongResourceSkipUpdate update);

private:
    static application::model::SongResourceQueueTarget mapTarget(boost::mysql::row_view row);

    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全 MySQL 连接池
    std::string claim_select_sql_;              // 主资源认领候选查询 SQL
    std::string claim_update_sql_;              // 主资源认领更新 SQL
    std::string related_claim_update_sql_;      // 关联资源认领更新 SQL
    std::string related_find_sql_;              // 关联资源查询 SQL
    std::string mark_succeeded_sql_;            // 成功回写 SQL
    std::string mark_failed_sql_;               // 失败回写 SQL
    std::string mark_skipped_sql_;              // 跳过回写 SQL
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
