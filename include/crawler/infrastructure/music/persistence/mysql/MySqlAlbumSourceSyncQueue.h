#pragma once

#include "crawler/application/ports/persistence/AlbumSourceSyncQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSqlPaths.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 专辑来源同步队列 adapter，只负责查询和更新详情/歌曲同步状态。
class MySqlAlbumSourceSyncQueue : public application::ports::AlbumSourceSyncQueue {
public:
    // 保存连接池并加载默认 SQL 模板。
    explicit MySqlAlbumSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池并加载指定 SQL 模板，便于测试或部署替换。
    MySqlAlbumSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool, MySqlAlbumSourceSqlPaths paths);

    // 查询需要补齐详情的专辑来源。
    std::vector<application::model::AlbumSourceMetadata> findPendingDetails(std::string source, std::size_t limit, std::size_t offset) override;

    // 更新详情同步状态。
    bool updateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error) override;

    // 查询需要获取歌曲列表的专辑来源。
    std::vector<application::model::AlbumSourceMetadata> findPendingSongs(std::string source, std::size_t limit, std::size_t offset) override;

    // 更新歌曲列表同步状态。
    bool updateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error) override;

    // 协程式查询需要补齐详情的专辑来源。
    boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> coFindPendingDetails(std::string source, std::size_t limit, std::size_t offset);

    // 协程式更新详情同步状态。
    boost::asio::awaitable<bool> coUpdateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error);

    // 协程式查询需要获取歌曲列表的专辑来源。
    boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> coFindPendingSongs(std::string source, std::size_t limit, std::size_t offset);

    // 协程式更新歌曲同步状态。
    boost::asio::awaitable<bool> coUpdateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池

    std::string update_detail_sync_sql_;     // 更新专辑详情同步状态语句
    std::string update_songs_sync_sql_;      // 更新专辑歌曲同步状态语句
    std::string select_pending_details_sql_; // 查询待补齐详情语句
    std::string select_pending_songs_sql_;   // 查询待获取歌曲语句
    std::string find_source_sql_;            // 零变更时区分已存在来源与无效目标
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
