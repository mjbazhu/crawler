#pragma once

#include "crawler/application/model/MusicCatalog.h"
#include "crawler/application/model/MusicOverview.h"
#include "crawler/application/ports/persistence/MusicCatalogQueryRepository.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 音乐目录查询 use case，供前端控制台读取库内状态。
class MusicCatalogQueryService {
public:
    // 保存只读查询仓储。
    explicit MusicCatalogQueryService(std::shared_ptr<application::ports::MusicCatalogQueryRepository> repository);

    // 查询 Migu 总览。
    application::model::MusicOverview overview() const;

    // 分页查询 Migu 歌手目录。
    application::model::MiguSingerCatalogPage listSingers(application::model::MiguSingerCatalogQuery query) const;

    // 按平台歌手 ID 查询 Migu 歌手详情。
    std::optional<application::model::MiguSingerCatalogRecord> findSinger(std::string source_singer_id) const;

    // 分页查询 Migu 专辑目录。
    application::model::MiguAlbumCatalogPage listAlbums(application::model::MiguAlbumCatalogQuery query) const;

    // 按平台专辑 ID 查询 Migu 专辑详情。
    std::optional<application::model::MiguAlbumCatalogRecord> findAlbum(std::string source_album_id) const;

    // 分页查询 Migu 歌曲目录。
    application::model::MiguSongCatalogPage listSongs(application::model::MiguSongCatalogQuery query) const;

    // 按平台歌曲 ID 查询 Migu 歌曲详情。
    std::optional<application::model::MiguSongCatalogRecord> findSong(std::string source_song_id) const;

    // 分页查询 Migu 资源队列。
    application::model::MiguResourceQueuePage listResources(application::model::MiguResourceQueueQuery query) const;

    // 按资源任务 ID 查询 Migu 资源队列记录。
    std::optional<application::model::MiguResourceQueueRecord> findResource(std::uint64_t resource_id) const;

    // 预览 Migu 资源治理影响范围。
    application::model::MiguResourcePreviewResult previewResources(application::model::MiguResourceActionQuery query) const;

    // 将 Migu 资源任务重新置为待获取。
    int retryResources(application::model::MiguResourceActionQuery query) const;

    // 重置 Migu 资源任务状态。
    int resetResources(application::model::MiguResourceActionQuery query) const;

    // 恢复超时停留在获取中的 Migu 资源任务。
    int recoverStaleResources(application::model::MiguResourceActionQuery query, std::string error) const;

    // 查询 Migu 元数据一致性摘要。
    application::model::MiguIntegritySummary integrity(int stale_after_seconds) const;

    // 查询 MySQL schema 版本和关键对象完整性。
    application::model::MiguSchemaStatus schemaStatus() const;

    // 写入治理审计事件。
    void appendGovernanceEvents(const std::vector<application::model::MiguGovernanceEventRecord> &events) const;

    // 查询治理审计事件。
    application::model::MiguGovernanceEventPage listGovernanceEvents(application::model::MiguGovernanceEventQuery query) const;

    // 查询 Migu 专辑来源元数据，供补齐 workflow 按显式 ID 启动。
    std::vector<application::model::AlbumSourceMetadata> findAlbumSourcesByIds(const std::vector<std::string> &source_album_ids) const;

    // 认领 Migu 歌手歌曲补池目标。
    application::model::MiguBackfillClaimResult claimSingerSongs(application::model::MiguBackfillTargetQuery query) const;

    // 认领 Migu 歌手专辑补池目标。
    application::model::MiguBackfillClaimResult claimSingerAlbums(application::model::MiguBackfillTargetQuery query) const;

    // 认领 Migu 专辑详情补池目标。
    application::model::MiguBackfillClaimResult claimAlbumDetails(application::model::MiguBackfillTargetQuery query) const;

    // 认领 Migu 专辑歌曲补池目标。
    application::model::MiguBackfillClaimResult claimAlbumSongs(application::model::MiguBackfillTargetQuery query) const;

    // 重置 Migu 歌手歌曲补池状态。
    int resetSingerSongs(application::model::MiguBackfillTargetQuery query) const;

    // 重置 Migu 歌手专辑补池状态。
    int resetSingerAlbums(application::model::MiguBackfillTargetQuery query) const;

    // 重置 Migu 专辑详情补池状态。
    int resetAlbumDetails(application::model::MiguBackfillTargetQuery query) const;

    // 重置 Migu 专辑歌曲补池状态。
    int resetAlbumSongs(application::model::MiguBackfillTargetQuery query) const;

    // 恢复超时停留在同步中的歌手歌曲目标。
    int recoverStaleSingerSongs(std::string source, int stale_after_seconds, std::string error) const;

    // 恢复超时停留在同步中的歌手专辑目标。
    int recoverStaleSingerAlbums(std::string source, int stale_after_seconds, std::string error) const;

    // 恢复超时停留在同步中的专辑详情目标。
    int recoverStaleAlbumDetails(std::string source, int stale_after_seconds, std::string error) const;

    // 恢复超时停留在同步中的专辑歌曲目标。
    int recoverStaleAlbumSongs(std::string source, int stale_after_seconds, std::string error) const;

private:
    std::shared_ptr<application::ports::MusicCatalogQueryRepository> repository_; // 音乐目录查询 port
};

} // namespace orchestrator::modules::crawler::application::use_cases
