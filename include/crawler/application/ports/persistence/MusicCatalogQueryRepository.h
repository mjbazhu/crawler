#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/MusicCatalog.h"
#include "crawler/application/model/MusicOverview.h"

#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 音乐目录只读查询 port，供前端控制台读取库内聚合、列表和补池状态。
class MusicCatalogQueryRepository {
public:
    virtual ~MusicCatalogQueryRepository() = default;

    // 查询 Migu 采集控制台总览。
    virtual application::model::MusicOverview overview() = 0;

    // 分页查询 Migu 歌手目录。
    virtual application::model::MiguSingerCatalogPage listSingers(application::model::MiguSingerCatalogQuery query) = 0;

    // 按平台歌手 ID 查询 Migu 歌手详情。
    virtual std::optional<application::model::MiguSingerCatalogRecord> findSinger(std::string source, std::string source_singer_id) = 0;

    // 分页查询 Migu 专辑目录。
    virtual application::model::MiguAlbumCatalogPage listAlbums(application::model::MiguAlbumCatalogQuery query) = 0;

    // 按平台专辑 ID 查询 Migu 专辑详情。
    virtual std::optional<application::model::MiguAlbumCatalogRecord> findAlbum(std::string source, std::string source_album_id) = 0;

    // 分页查询 Migu 歌曲目录。
    virtual application::model::MiguSongCatalogPage listSongs(application::model::MiguSongCatalogQuery query) = 0;

    // 按平台歌曲 ID 查询 Migu 歌曲详情。
    virtual std::optional<application::model::MiguSongCatalogRecord> findSong(std::string source, std::string source_song_id) = 0;

    // 分页查询资源任务队列。
    virtual application::model::MiguResourceQueuePage listResources(application::model::MiguResourceQueueQuery query) = 0;

    // 按资源任务 ID 查询资源队列记录。
    virtual std::optional<application::model::MiguResourceQueueRecord> findResource(std::uint64_t resource_id) = 0;

    // 预览资源治理影响范围，不写库。
    virtual application::model::MiguResourcePreviewResult previewResources(application::model::MiguResourceActionQuery query) = 0;

    // 将失败或跳过的资源任务重新置为待获取，返回更新数量。
    virtual int retryResources(application::model::MiguResourceActionQuery query) = 0;

    // 重置资源任务状态和重试次数，返回更新数量。
    virtual int resetResources(application::model::MiguResourceActionQuery query) = 0;

    // 恢复超时停留在获取中的资源任务，返回更新数量。
    virtual int recoverStaleResources(application::model::MiguResourceActionQuery query, std::string error) = 0;

    // 查询元数据一致性摘要。
    virtual application::model::MiguIntegritySummary integrity(std::string source, int stale_after_seconds) = 0;

    // 查询 MySQL schema 版本和关键对象完整性。
    virtual application::model::MiguSchemaStatus schemaStatus() = 0;

    // 写入治理审计事件。
    virtual void appendGovernanceEvents(const std::vector<application::model::MiguGovernanceEventRecord> &events) = 0;

    // 查询治理审计事件。
    virtual application::model::MiguGovernanceEventPage listGovernanceEvents(application::model::MiguGovernanceEventQuery query) = 0;

    // 按平台专辑 ID 查询补齐 workflow 所需的专辑来源元数据。
    virtual std::vector<application::model::AlbumSourceMetadata> findAlbumSourcesByIds(std::string source, const std::vector<std::string> &source_album_ids) = 0;

    // 按条件认领歌手歌曲补池目标，只有状态允许的目标会被置为同步中。
    virtual application::model::MiguBackfillClaimResult claimSingerSongs(application::model::MiguBackfillTargetQuery query) = 0;

    // 按条件认领歌手专辑补池目标，只有状态允许的目标会被置为同步中。
    virtual application::model::MiguBackfillClaimResult claimSingerAlbums(application::model::MiguBackfillTargetQuery query) = 0;

    // 按条件认领专辑详情补池目标，只有状态允许的目标会被置为同步中。
    virtual application::model::MiguBackfillClaimResult claimAlbumDetails(application::model::MiguBackfillTargetQuery query) = 0;

    // 按条件认领专辑歌曲补池目标，只有状态允许且详情已完成的目标会被置为同步中。
    virtual application::model::MiguBackfillClaimResult claimAlbumSongs(application::model::MiguBackfillTargetQuery query) = 0;

    // 重置歌手歌曲补池状态，返回更新数量。
    virtual int resetSingerSongs(application::model::MiguBackfillTargetQuery query) = 0;

    // 重置歌手专辑补池状态，返回更新数量。
    virtual int resetSingerAlbums(application::model::MiguBackfillTargetQuery query) = 0;

    // 重置专辑详情补池状态，返回更新数量。
    virtual int resetAlbumDetails(application::model::MiguBackfillTargetQuery query) = 0;

    // 重置专辑歌曲补池状态，返回更新数量。
    virtual int resetAlbumSongs(application::model::MiguBackfillTargetQuery query) = 0;

    // 恢复超时停留在同步中的歌手歌曲目标，返回更新数量。
    virtual int recoverStaleSingerSongs(std::string source, int stale_after_seconds, std::string error) = 0;

    // 恢复超时停留在同步中的歌手专辑目标，返回更新数量。
    virtual int recoverStaleSingerAlbums(std::string source, int stale_after_seconds, std::string error) = 0;

    // 恢复超时停留在同步中的专辑详情目标，返回更新数量。
    virtual int recoverStaleAlbumDetails(std::string source, int stale_after_seconds, std::string error) = 0;

    // 恢复超时停留在同步中的专辑歌曲目标，返回更新数量。
    virtual int recoverStaleAlbumSongs(std::string source, int stale_after_seconds, std::string error) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
