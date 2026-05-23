#pragma once

#include "crawler/application/ports/persistence/SingerSourceSyncQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 歌手来源同步队列 adapter，只负责查询、认领和更新歌曲/专辑列表同步状态。
class MySqlSingerSourceSyncQueue : public application::ports::SingerSourceSyncQueue {
public:
    // 歌手来源同步队列所需 SQL 文件路径。
    struct SqlPaths {
        std::string select_pending_songs = "sql/mysql/26_select_singer_sources_pending_songs_filtered.sql";   // 查询待同步歌曲列表歌手
        std::string select_pending_albums = "sql/mysql/27_select_singer_sources_pending_albums_filtered.sql"; // 查询待同步专辑列表歌手
        std::string count_pending_songs = "sql/mysql/30_count_singer_sources_pending_songs_filtered.sql";     // 统计歌曲列表同步预览范围
        std::string count_pending_albums = "sql/mysql/31_count_singer_sources_pending_albums_filtered.sql";   // 统计专辑列表同步预览范围
        std::string claim_pending_songs = "sql/mysql/28_claim_singer_source_songs_sync_status.sql";           // 认领歌曲同步目标
        std::string claim_pending_albums = "sql/mysql/29_claim_singer_source_albums_sync_status.sql";         // 认领专辑同步目标
        std::string update_songs_sync_status = "sql/mysql/09_update_singer_source_songs_sync_status.sql";     // 更新歌曲同步状态
        std::string update_albums_sync_status = "sql/mysql/10_update_singer_source_albums_sync_status.sql";   // 更新专辑同步状态
    };

    // 保存连接池并加载默认 SQL 模板。
    explicit MySqlSingerSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池并加载指定 SQL 模板，便于测试或部署替换。
    MySqlSingerSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    // 认领需要同步歌曲列表的歌手来源。
    std::vector<application::model::SingerSourceSyncTarget> claimPendingSongs(application::model::SingerSourceSyncQuery query) override;

    // 认领需要同步专辑列表的歌手来源。
    std::vector<application::model::SingerSourceSyncTarget> claimPendingAlbums(application::model::SingerSourceSyncQuery query) override;

    // 预览需要同步歌曲列表的歌手来源，不修改状态。
    application::model::SingerSourceSyncPreviewResult previewPendingSongs(application::model::SingerSourceSyncQuery query) override;

    // 预览需要同步专辑列表的歌手来源，不修改状态。
    application::model::SingerSourceSyncPreviewResult previewPendingAlbums(application::model::SingerSourceSyncQuery query) override;

    // 更新歌曲列表同步状态。
    bool updateSongsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error) override;

    // 更新专辑列表同步状态。
    bool updateAlbumsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error) override;

    // 协程式认领歌曲同步目标。
    boost::asio::awaitable<std::vector<application::model::SingerSourceSyncTarget>> coClaimPendingSongs(application::model::SingerSourceSyncQuery query);

    // 协程式认领专辑同步目标。
    boost::asio::awaitable<std::vector<application::model::SingerSourceSyncTarget>> coClaimPendingAlbums(application::model::SingerSourceSyncQuery query);

    // 协程式预览歌曲同步目标。
    boost::asio::awaitable<application::model::SingerSourceSyncPreviewResult> coPreviewPendingSongs(application::model::SingerSourceSyncQuery query);

    // 协程式预览专辑同步目标。
    boost::asio::awaitable<application::model::SingerSourceSyncPreviewResult> coPreviewPendingAlbums(application::model::SingerSourceSyncQuery query);

    // 协程式更新歌曲同步状态。
    boost::asio::awaitable<bool> coUpdateSongsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error);

    // 协程式更新专辑同步状态。
    boost::asio::awaitable<bool> coUpdateAlbumsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池

    std::string select_pending_songs_sql_;  // 查询待同步歌曲列表歌手语句
    std::string select_pending_albums_sql_; // 查询待同步专辑列表歌手语句
    std::string count_pending_songs_sql_;   // 统计歌曲列表同步预览语句
    std::string count_pending_albums_sql_;  // 统计专辑列表同步预览语句
    std::string claim_pending_songs_sql_;   // 认领歌曲同步目标语句
    std::string claim_pending_albums_sql_;  // 认领专辑同步目标语句
    std::string update_songs_sync_sql_;     // 更新歌曲同步状态语句
    std::string update_albums_sync_sql_;    // 更新专辑同步状态语句
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
