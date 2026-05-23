#pragma once

#include "crawler/application/ports/persistence/AlbumSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSqlPaths.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

class MySqlAlbumSourceSyncQueue;
class MySqlAlbumSourceWriter;

// MySQL 专辑来源仓储，使用外置 SQL 模板写入 albums、album_source_refs 和平台扩展表。
class MySqlAlbumSourceRepository : public application::ports::AlbumSourceRepository {
public:
    using SqlPaths = MySqlAlbumSourceSqlPaths;

    // 保存连接池和默认 SQL 模板。
    explicit MySqlAlbumSourceRepository(std::shared_ptr<MySqlConnectionPool> pool);

    // 保存连接池和指定 SQL 模板，便于测试或部署时替换模板路径。
    MySqlAlbumSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    // 释放内部 writer 和 sync queue 兼容组件。
    ~MySqlAlbumSourceRepository() override;

    // 同步 port 入口：内部桥接到 MySQL 协程写入并阻塞等待结果。
    bool saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums) override;

    // 查询需要补齐详情的专辑来源。
    std::vector<application::model::AlbumSourceMetadata> findPendingDetails(std::string source, std::size_t limit, std::size_t offset) override;

    // 更新详情同步状态。
    bool updateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error) override;

    // 查询需要获取歌曲列表的专辑来源。
    std::vector<application::model::AlbumSourceMetadata> findPendingSongs(std::string source, std::size_t limit, std::size_t offset) override;

    // 更新歌曲列表同步状态。
    bool updateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error) override;

    // 协程式批量保存专辑来源，单批次使用同一事务。
    boost::asio::awaitable<bool> coSaveBatch(std::vector<application::model::AlbumSourceMetadata> albums);

    // 协程式查询需要补齐详情的专辑来源。
    boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> coFindPendingDetails(std::string source, std::size_t limit, std::size_t offset);

    // 协程式更新详情同步状态。
    boost::asio::awaitable<bool> coUpdateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error);

    // 协程式查询需要获取歌曲列表的专辑来源。
    boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> coFindPendingSongs(std::string source, std::size_t limit, std::size_t offset);

    // 协程式更新歌曲同步状态。
    boost::asio::awaitable<bool> coUpdateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error);

private:
    std::unique_ptr<MySqlAlbumSourceWriter> writer_;         // 专辑来源写入 adapter
    std::unique_ptr<MySqlAlbumSourceSyncQueue> sync_queue_;  // 专辑来源同步队列 adapter
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
