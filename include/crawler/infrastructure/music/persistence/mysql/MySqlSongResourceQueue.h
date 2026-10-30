#pragma once
#include <cstdint>

#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/row_view.hpp>

#include "crawler/application/model/SongResourceFile.h"

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
        std::string claim_select = "sql/mysql/song_resources/claim_select.sql";                 // 主资源认领候选查询
        std::string claim_update = "sql/mysql/song_resources/claim_update.sql";                 // 主资源认领状态更新
        std::string related_claim_update = "sql/mysql/song_resources/related_claim_update.sql"; // 关联资源认领更新
        std::string related_find = "sql/mysql/song_resources/related_find.sql";                 // 关联资源认领后查询
        std::string mark_succeeded = "sql/mysql/song_resources/mark_succeeded.sql";             // 成功回写
        std::string mark_failed = "sql/mysql/song_resources/mark_failed.sql";                   // 失败回写
        std::string mark_skipped = "sql/mysql/song_resources/mark_skipped.sql";                 // 跳过回写
        bool enforce_dispatch = false;                                                          // 正式组合根必须启用；旧独立 adapter 调用保留兼容
        std::string legacy_cloud_provider;                                                      // 旧rclone平台必须显式声明baidu/gdrive
    };

    // 启动文件补偿核对，只返回归属事实，不直接操作文件系统。
    std::optional<application::model::ResourceWriteDisposition> inspectResourceWrite(std::uint64_t resource, std::uint64_t generation, const std::string &path);

    // 保存连接池并加载默认 SQL。
    explicit MySqlSongResourceQueue(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池并加载指定 SQL，便于测试替换。
    MySqlSongResourceQueue(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    std::vector<application::model::SongResourceQueueTarget> claimPending(application::model::SongResourceClaimQuery query) override;
    std::optional<application::model::SongResourceQueueTarget> claimRelated(application::model::SongResourceRelatedClaimRequest request) override;
    bool markSucceeded(application::model::SongResourceSuccessUpdate update) override;
    // 回写异常后读取实际提交状态，避免删除可能已被数据库引用的文件。
    application::result::ServiceResult<bool> confirmSucceeded(application::model::SongResourceSuccessUpdate update) override;
    bool markFailed(application::model::SongResourceFailureUpdate update) override;
    bool markSkipped(application::model::SongResourceSkipUpdate update) override;

    boost::asio::awaitable<std::vector<application::model::SongResourceQueueTarget>> coClaimPending(application::model::SongResourceClaimQuery query);
    boost::asio::awaitable<std::optional<application::model::SongResourceQueueTarget>> coClaimRelated(application::model::SongResourceRelatedClaimRequest request);
    boost::asio::awaitable<bool> coMarkSucceeded(application::model::SongResourceSuccessUpdate update);
    boost::asio::awaitable<bool> coMarkFailed(application::model::SongResourceFailureUpdate update);
    boost::asio::awaitable<bool> coMarkSkipped(application::model::SongResourceSkipUpdate update);

    // 同一投影供旧队列与独立来源读取复用，避免字段顺序漂移。
    static application::model::SongResourceQueueTarget mapTarget(boost::mysql::row_view row);

private:
    // 使用独立查询确认模糊的成功结果，返回 NULL 代表数据库暂时不可用。
    boost::asio::awaitable<std::optional<bool>> coConfirmSucceeded(application::model::SongResourceSuccessUpdate update);

    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全 MySQL 连接池
    bool enforce_dispatch_ = false;             // 在认领 SQL 本身核对持久维护及 local 目标状态
    std::string legacy_cloud_provider_;         // 旧单云回写的平台归属，不从工具名推测
    std::string claim_select_sql_;              // 主资源认领候选查询 SQL
    std::string claim_update_sql_;              // 主资源认领更新 SQL
    std::string related_claim_update_sql_;      // 关联资源认领更新 SQL
    std::string related_find_sql_;              // 关联资源查询 SQL
    std::string mark_succeeded_sql_;            // 成功回写 SQL
    std::string mark_failed_sql_;               // 失败回写 SQL
    std::string mark_skipped_sql_;              // 跳过回写 SQL
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
