#pragma once

#include "crawler/application/ports/persistence/MusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/row_view.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 音乐目录查询仓储，负责控制台总览聚合。
class MySqlMusicCatalogQueryRepository : public application::ports::MusicCatalogQueryRepository {
public:
    // 查询 SQL 文件路径集合。
    struct SqlPaths {
        std::string singer_list = "sql/mysql/catalog/migu_singer_list.sql";                       // 歌手列表查询
        std::string singer_count = "sql/mysql/catalog/migu_singer_count.sql";                     // 歌手列表计数
        std::string singer_find = "sql/mysql/catalog/migu_singer_find.sql";                       // 歌手详情查询
        std::string album_list = "sql/mysql/catalog/migu_album_list.sql";                         // 专辑列表查询
        std::string album_count = "sql/mysql/catalog/migu_album_count.sql";                       // 专辑列表计数
        std::string album_find = "sql/mysql/catalog/migu_album_find.sql";                         // 专辑详情查询
        std::string song_list = "sql/mysql/catalog/migu_song_list.sql";                           // 歌曲列表查询
        std::string song_count = "sql/mysql/catalog/migu_song_count.sql";                         // 歌曲列表计数
        std::string song_find = "sql/mysql/catalog/migu_song_find.sql";                           // 歌曲详情查询
        std::string resource_list = "sql/mysql/catalog/migu_resource_queue_list.sql";             // 资源队列查询
        std::string resource_count = "sql/mysql/catalog/migu_resource_queue_count.sql";           // 资源队列计数
        std::string resource_find = "sql/mysql/catalog/migu_resource_queue_find.sql";             // 资源队列详情查询
        std::string resource_retry = "sql/mysql/catalog/migu_resource_retry.sql";                 // 资源队列重试
        std::string resource_reset = "sql/mysql/catalog/migu_resource_reset.sql";                 // 资源队列重置
        std::string resource_recover_stale = "sql/mysql/catalog/migu_resource_recover_stale.sql"; // 资源队列超时恢复
        std::string governance_event_insert = "sql/mysql/catalog/migu_governance_event_insert.sql"; // 治理审计写入
        std::string governance_event_list = "sql/mysql/catalog/migu_governance_event_list.sql";   // 治理审计列表
        std::string governance_event_count = "sql/mysql/catalog/migu_governance_event_count.sql"; // 治理审计计数
        std::string integrity = "sql/mysql/catalog/migu_integrity.sql";                           // 一致性摘要查询
        std::string schema_status = "sql/mysql/catalog/migu_schema_status.sql";                   // schema 版本和完整性查询
        std::string album_source_find = "sql/mysql/catalog/migu_album_source_find.sql";           // 专辑来源查询
        std::string claim_singer_songs_select = "sql/mysql/catalog/migu_claim_singer_songs_select.sql";   // 歌手歌曲认领候选查询
        std::string claim_singer_songs_update = "sql/mysql/catalog/migu_claim_singer_songs_update.sql";   // 歌手歌曲认领更新
        std::string claim_singer_albums_select = "sql/mysql/catalog/migu_claim_singer_albums_select.sql"; // 歌手专辑认领候选查询
        std::string claim_singer_albums_update = "sql/mysql/catalog/migu_claim_singer_albums_update.sql"; // 歌手专辑认领更新
        std::string claim_album_details_select = "sql/mysql/catalog/migu_claim_album_details_select.sql"; // 专辑详情认领候选查询
        std::string claim_album_details_update = "sql/mysql/catalog/migu_claim_album_details_update.sql"; // 专辑详情认领更新
        std::string claim_album_songs_select = "sql/mysql/catalog/migu_claim_album_songs_select.sql";     // 专辑歌曲认领候选查询
        std::string claim_album_songs_update = "sql/mysql/catalog/migu_claim_album_songs_update.sql";     // 专辑歌曲认领更新
        std::string reset_singer_songs = "sql/mysql/catalog/migu_reset_singer_songs.sql";         // 重置歌手歌曲状态
        std::string reset_singer_albums = "sql/mysql/catalog/migu_reset_singer_albums.sql";       // 重置歌手专辑状态
        std::string reset_album_details = "sql/mysql/catalog/migu_reset_album_details.sql";       // 重置专辑详情状态
        std::string reset_album_songs = "sql/mysql/catalog/migu_reset_album_songs.sql";           // 重置专辑歌曲状态
        std::string recover_singer_songs = "sql/mysql/catalog/migu_recover_stale_singer_songs.sql"; // 恢复歌手歌曲超时同步
        std::string recover_singer_albums = "sql/mysql/catalog/migu_recover_stale_singer_albums.sql"; // 恢复歌手专辑超时同步
        std::string recover_album_details = "sql/mysql/catalog/migu_recover_stale_album_details.sql"; // 恢复专辑详情超时同步
        std::string recover_album_songs = "sql/mysql/catalog/migu_recover_stale_album_songs.sql"; // 恢复专辑歌曲超时同步
    };

    // 保存线程安全 MySQL 连接池。
    explicit MySqlMusicCatalogQueryRepository(std::shared_ptr<MySqlConnectionPool> pool);
    MySqlMusicCatalogQueryRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths);

    application::model::MusicOverview overview() override;
    application::model::MiguSingerCatalogPage listSingers(application::model::MiguSingerCatalogQuery query) override;
    std::optional<application::model::MiguSingerCatalogRecord> findSinger(std::string source, std::string source_singer_id) override;
    application::model::MiguAlbumCatalogPage listAlbums(application::model::MiguAlbumCatalogQuery query) override;
    std::optional<application::model::MiguAlbumCatalogRecord> findAlbum(std::string source, std::string source_album_id) override;
    application::model::MiguSongCatalogPage listSongs(application::model::MiguSongCatalogQuery query) override;
    std::optional<application::model::MiguSongCatalogRecord> findSong(std::string source, std::string source_song_id) override;
    application::model::MiguResourceQueuePage listResources(application::model::MiguResourceQueueQuery query) override;
    std::optional<application::model::MiguResourceQueueRecord> findResource(std::uint64_t resource_id) override;
    application::model::MiguResourcePreviewResult previewResources(application::model::MiguResourceActionQuery query) override;
    int retryResources(application::model::MiguResourceActionQuery query) override;
    int resetResources(application::model::MiguResourceActionQuery query) override;
    int recoverStaleResources(application::model::MiguResourceActionQuery query, std::string error) override;
    application::model::MiguIntegritySummary integrity(std::string source, int stale_after_seconds) override;
    application::model::MiguSchemaStatus schemaStatus() override;
    void appendGovernanceEvents(const std::vector<application::model::MiguGovernanceEventRecord> &events) override;
    application::model::MiguGovernanceEventPage listGovernanceEvents(application::model::MiguGovernanceEventQuery query) override;
    std::vector<application::model::AlbumSourceMetadata> findAlbumSourcesByIds(std::string source, const std::vector<std::string> &source_album_ids) override;
    application::model::MiguBackfillClaimResult claimSingerSongs(application::model::MiguBackfillTargetQuery query) override;
    application::model::MiguBackfillClaimResult claimSingerAlbums(application::model::MiguBackfillTargetQuery query) override;
    application::model::MiguBackfillClaimResult claimAlbumDetails(application::model::MiguBackfillTargetQuery query) override;
    application::model::MiguBackfillClaimResult claimAlbumSongs(application::model::MiguBackfillTargetQuery query) override;
    int resetSingerSongs(application::model::MiguBackfillTargetQuery query) override;
    int resetSingerAlbums(application::model::MiguBackfillTargetQuery query) override;
    int resetAlbumDetails(application::model::MiguBackfillTargetQuery query) override;
    int resetAlbumSongs(application::model::MiguBackfillTargetQuery query) override;
    int recoverStaleSingerSongs(std::string source, int stale_after_seconds, std::string error) override;
    int recoverStaleSingerAlbums(std::string source, int stale_after_seconds, std::string error) override;
    int recoverStaleAlbumDetails(std::string source, int stale_after_seconds, std::string error) override;
    int recoverStaleAlbumSongs(std::string source, int stale_after_seconds, std::string error) override;

private:
    boost::asio::awaitable<application::model::MusicOverview> coOverview();
    boost::asio::awaitable<application::model::MiguSingerCatalogPage> coListSingers(application::model::MiguSingerCatalogQuery query);
    boost::asio::awaitable<std::optional<application::model::MiguSingerCatalogRecord>> coFindSinger(std::string source, std::string source_singer_id);
    boost::asio::awaitable<application::model::MiguAlbumCatalogPage> coListAlbums(application::model::MiguAlbumCatalogQuery query);
    boost::asio::awaitable<std::optional<application::model::MiguAlbumCatalogRecord>> coFindAlbum(std::string source, std::string source_album_id);
    boost::asio::awaitable<application::model::MiguSongCatalogPage> coListSongs(application::model::MiguSongCatalogQuery query);
    boost::asio::awaitable<std::optional<application::model::MiguSongCatalogRecord>> coFindSong(std::string source, std::string source_song_id);
    boost::asio::awaitable<application::model::MiguResourceQueuePage> coListResources(application::model::MiguResourceQueueQuery query);
    boost::asio::awaitable<std::optional<application::model::MiguResourceQueueRecord>> coFindResource(std::uint64_t resource_id);
    boost::asio::awaitable<application::model::MiguResourcePreviewResult> coPreviewResources(application::model::MiguResourceActionQuery query);
    boost::asio::awaitable<int> coRetryResources(application::model::MiguResourceActionQuery query);
    boost::asio::awaitable<int> coResetResources(application::model::MiguResourceActionQuery query);
    boost::asio::awaitable<int> coRecoverStaleResources(application::model::MiguResourceActionQuery query, std::string error);
    boost::asio::awaitable<application::model::MiguIntegritySummary> coIntegrity(std::string source, int stale_after_seconds);
    boost::asio::awaitable<application::model::MiguSchemaStatus> coSchemaStatus();
    boost::asio::awaitable<bool> coAppendGovernanceEvents(std::vector<application::model::MiguGovernanceEventRecord> events);
    boost::asio::awaitable<application::model::MiguGovernanceEventPage> coListGovernanceEvents(application::model::MiguGovernanceEventQuery query);
    boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> coFindAlbumSourcesByIds(std::string source, std::vector<std::string> source_album_ids);
    boost::asio::awaitable<application::model::MiguBackfillClaimResult> coClaimSingerSongs(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<application::model::MiguBackfillClaimResult> coClaimSingerAlbums(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<application::model::MiguBackfillClaimResult> coClaimAlbumDetails(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<application::model::MiguBackfillClaimResult> coClaimAlbumSongs(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<int> coResetSingerSongs(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<int> coResetSingerAlbums(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<int> coResetAlbumDetails(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<int> coResetAlbumSongs(application::model::MiguBackfillTargetQuery query);
    boost::asio::awaitable<int> coRecoverStaleSingerSongs(std::string source, int stale_after_seconds, std::string error);
    boost::asio::awaitable<int> coRecoverStaleSingerAlbums(std::string source, int stale_after_seconds, std::string error);
    boost::asio::awaitable<int> coRecoverStaleAlbumDetails(std::string source, int stale_after_seconds, std::string error);
    boost::asio::awaitable<int> coRecoverStaleAlbumSongs(std::string source, int stale_after_seconds, std::string error);

    static application::model::MiguSingerCatalogRecord mapSinger(boost::mysql::row_view row);
    static application::model::MiguAlbumCatalogRecord mapAlbum(boost::mysql::row_view row);
    static application::model::MiguSongCatalogRecord mapSong(boost::mysql::row_view row);
    static application::model::MiguResourceQueueRecord mapResource(boost::mysql::row_view row);
    static application::model::MiguGovernanceEventRecord mapGovernanceEvent(boost::mysql::row_view row);
    static application::model::AlbumSourceMetadata mapAlbumSource(boost::mysql::row_view row);

    std::shared_ptr<MySqlConnectionPool> pool_; // MySQL 连接池
    std::string singer_list_sql_;               // 歌手列表 SQL
    std::string singer_count_sql_;              // 歌手计数 SQL
    std::string singer_find_sql_;               // 歌手详情 SQL
    std::string album_list_sql_;                // 专辑列表 SQL
    std::string album_count_sql_;               // 专辑计数 SQL
    std::string album_find_sql_;                // 专辑详情 SQL
    std::string song_list_sql_;                 // 歌曲列表 SQL
    std::string song_count_sql_;                // 歌曲计数 SQL
    std::string song_find_sql_;                 // 歌曲详情 SQL
    std::string resource_list_sql_;             // 资源列表 SQL
    std::string resource_count_sql_;            // 资源计数 SQL
    std::string resource_find_sql_;             // 资源详情 SQL
    std::string resource_retry_sql_;            // 资源重试 SQL
    std::string resource_reset_sql_;            // 资源重置 SQL
    std::string resource_recover_stale_sql_;    // 资源超时恢复 SQL
    std::string governance_event_insert_sql_;   // 治理审计写入 SQL
    std::string governance_event_list_sql_;     // 治理审计列表 SQL
    std::string governance_event_count_sql_;    // 治理审计计数 SQL
    std::string integrity_sql_;                 // 一致性摘要 SQL
    std::string schema_status_sql_;             // schema 版本和完整性 SQL
    std::string album_source_find_sql_;         // 专辑来源 SQL
    std::string claim_singer_songs_select_sql_; // 歌手歌曲认领候选 SQL
    std::string claim_singer_songs_update_sql_; // 歌手歌曲认领更新 SQL
    std::string claim_singer_albums_select_sql_; // 歌手专辑认领候选 SQL
    std::string claim_singer_albums_update_sql_; // 歌手专辑认领更新 SQL
    std::string claim_album_details_select_sql_; // 专辑详情认领候选 SQL
    std::string claim_album_details_update_sql_; // 专辑详情认领更新 SQL
    std::string claim_album_songs_select_sql_;   // 专辑歌曲认领候选 SQL
    std::string claim_album_songs_update_sql_;   // 专辑歌曲认领更新 SQL
    std::string reset_singer_songs_sql_;        // 重置歌手歌曲 SQL
    std::string reset_singer_albums_sql_;       // 重置歌手专辑 SQL
    std::string reset_album_details_sql_;       // 重置专辑详情 SQL
    std::string reset_album_songs_sql_;         // 重置专辑歌曲 SQL
    std::string recover_singer_songs_sql_;      // 恢复歌手歌曲超时 SQL
    std::string recover_singer_albums_sql_;     // 恢复歌手专辑超时 SQL
    std::string recover_album_details_sql_;     // 恢复专辑详情超时 SQL
    std::string recover_album_songs_sql_;       // 恢复专辑歌曲超时 SQL
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
