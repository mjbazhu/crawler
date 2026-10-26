#pragma once

#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 专辑来源 adapter 所需的外置 SQL 文件路径。
struct MySqlAlbumSourceSqlPaths {
    std::string transaction_begin = "sql/mysql/transactions/transaction_begin.sql";                                   // 开启事务 SQL
    std::string transaction_commit = "sql/mysql/transactions/transaction_commit.sql";                                 // 提交事务 SQL
    std::string transaction_rollback = "sql/mysql/transactions/transaction_rollback.sql";                             // 回滚事务 SQL
    std::string update_detail_sync_status = "sql/mysql/album_source_refs/update_album_source_detail_sync_status.sql"; // 更新详情同步状态 SQL
    std::string update_songs_sync_status = "sql/mysql/album_source_refs/update_album_source_songs_sync_status.sql";   // 更新歌曲同步状态 SQL
    std::string select_pending_details = "sql/mysql/album_source_refs/select_album_sources_pending_details.sql";      // 查询待补齐详情 SQL
    std::string select_pending_songs = "sql/mysql/album_source_refs/select_album_sources_pending_songs.sql";          // 查询待获取歌曲 SQL
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
