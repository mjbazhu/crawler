#include "crawler/application/use_cases/AlbumDetailBackfillWorkflow.h"

#include "crawler/application/use_cases/WorkflowPaging.h"
#include "AlbumCollectionCompletion.h"
#include "AlbumTrackCollection.h"

#include <future>
#include <algorithm>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

AlbumDetailBackfillWorkflow::AlbumDetailBackfillWorkflow(
    std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider,
    std::shared_ptr<application::ports::AlbumSourceSyncQueue> album_source_sync_queue,
    std::shared_ptr<ParserDispatch> parser,
    std::shared_ptr<MetadataPersistenceService> persistence,
    std::shared_ptr<application::ports::TaskExecutor> page_executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
) : album_detail_provider_(std::move(album_detail_provider)),
    album_source_sync_queue_(std::move(album_source_sync_queue)),
    parser_(std::move(parser)),
    persistence_(std::move(persistence)),
    page_runner_(std::move(page_executor), std::move(diagnostics))
{}

bool AlbumDetailBackfillWorkflow::crawlPendingAlbumDetailsAndSave(std::string source, int limit, int max_page_concurrency)
{
    return collectPending(std::move(source), limit, max_page_concurrency, false);
}

bool AlbumDetailBackfillWorkflow::crawlPendingAlbumSongsAndSave(std::string source, int limit, int max_page_concurrency)
{
    return collectPending(std::move(source), limit, max_page_concurrency, true);
}

bool AlbumDetailBackfillWorkflow::collectPending(std::string source, int limit, int max_page_concurrency, bool songs_only)
{
    if (!album_detail_provider_ || !album_source_sync_queue_ || !parser_ || !persistence_ || source.empty()) return false;
    const auto pending = songs_only ? pendingAlbumSongs(source, limit) : pendingAlbumDetails(source, limit);
    if (pending.empty()) return true;
    const auto window = static_cast<std::size_t>(workflowPageWindow(max_page_concurrency));
    bool saved = false;
    for (std::size_t first = 0; first < pending.size(); first += window) {
        std::vector<std::future<bool>> tasks;
        for (std::size_t i = first; i < (std::min)(pending.size(), first + window); ++i) {
            tasks.push_back(page_runner_.submit<bool>([this, item = pending[i], songs_only] {
                return songs_only ? crawlAlbumSongsAndSave(item) : crawlAlbumDetailAndSave(item);
            },
                                                      false,
                                                      "collect pending album"));
        }
        for (auto &task : tasks) saved = task.get() || saved;
    }
    return saved;
}

std::vector<application::model::AlbumSourceMetadata> AlbumDetailBackfillWorkflow::pendingAlbumDetails(std::string source, int limit)
{
    if (!album_source_sync_queue_ || source.empty()) {
        return {};
    }
    const int normalized_limit = normalizeWorkflowLimit(limit);
    return album_source_sync_queue_->findPendingDetails(std::move(source), static_cast<std::size_t>(normalized_limit), 0);
}

std::vector<application::model::AlbumSourceMetadata> AlbumDetailBackfillWorkflow::pendingAlbumSongs(std::string source, int limit)
{
    if (!album_source_sync_queue_ || source.empty()) {
        return {};
    }
    const int normalized_limit = normalizeWorkflowLimit(limit);
    return album_source_sync_queue_->findPendingSongs(std::move(source), static_cast<std::size_t>(normalized_limit), 0);
}

bool AlbumDetailBackfillWorkflow::crawlAlbumDetailAndSave(application::model::AlbumSourceMetadata source_metadata)
{
    return collectAlbumResponse(std::move(source_metadata), false);
}

bool AlbumDetailBackfillWorkflow::crawlAlbumSongsAndSave(application::model::AlbumSourceMetadata source_metadata)
{
    return collectAlbumResponse(std::move(source_metadata), true);
}

bool AlbumDetailBackfillWorkflow::collectAlbumResponse(application::model::AlbumSourceMetadata source_metadata, bool songs_only)
{
    if (!album_detail_provider_ || !album_source_sync_queue_ || !parser_ || !persistence_ || source_metadata.source_album_id.empty()) {
        return false;
    }

    const auto fail = [&](const std::string &message) {
        if (songs_only)
            (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, 0, message);
        else
            (void)album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, message);
        return false;
    };
    if (songs_only)
        (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, 0, {});
    else
        (void)album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, {});
    auto response = album_detail_provider_->fetchAlbumDetail(source_metadata.source_album_id, source_metadata.source_res_type);
    if (!response || response->status_code < 200 || response->status_code >= 300) return fail("album response fetch failed");

    auto batch = parser_->parseSources(std::move(response->body));
    auto target = std::find_if(batch.albums.begin(), batch.albums.end(), [&](const auto &album) {
        return album.detail_sync_status == 2 && album.source == source_metadata.source && (album.source_album_id == source_metadata.source_album_id || album.source_material_id == source_metadata.source_album_id);
    });
    if (target == batch.albums.end()) return fail("album response identity or metadata missing");
    const auto pagination_error = collectRemainingAlbumTracks(*target, batch, *album_detail_provider_, *parser_);
    for (auto &album : batch.albums) {
        // 响应中的推荐专辑卡片仍是摘要，不能被本次详情任务误标为已采集。
        if (album.detail_sync_status == 2) album.songs_sync = albumSongSyncResult(album, batch.songs);
    }
    if (!pagination_error.empty()) {
        target->songs_sync->status = 4;
        target->songs_sync->error = pagination_error;
    }
    const auto result = *target->songs_sync;
    if (!persistence_->saveAlbumDetails(std::move(batch))) return fail("album response save failed");
    // 原子 writer 已提交两个独立结果，不能再次以 UPDATE 的变更行数推断事务成功。
    if (persistence_->hasAtomicWriter()) return true;
    const bool detail_saved = album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 2, {});
    const bool songs_saved = album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, result.status, result.received_count, result.error);
    return detail_saved && songs_saved;
}

} // namespace orchestrator::modules::crawler::application::use_cases
