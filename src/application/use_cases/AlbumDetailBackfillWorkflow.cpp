#include "crawler/application/use_cases/AlbumDetailBackfillWorkflow.h"

#include "crawler/application/use_cases/WorkflowPaging.h"
#include "AlbumCollectionCompletion.h"

#include <future>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 专辑详情采集结果。
struct AlbumDetailResult {
    application::model::AlbumSourceMetadata source;                      // 请求来源
    std::vector<application::model::AlbumSourceMetadata> detail_sources; // 详情解析结果
    std::vector<application::model::SongSourceMetadata> song_sources;    // 歌曲来源解析结果
    bool fetched = false;                                                // 是否成功获取详情
};

// 专辑歌曲采集结果。
struct AlbumSongResult {
    application::model::AlbumSourceMetadata source;                      // 请求来源
    std::vector<application::model::SongSourceMetadata> song_sources;    // 歌曲来源解析结果
    bool fetched = false;                                                // 是否成功获取响应
    std::vector<application::model::AlbumSourceMetadata> detail_sources; // 同响应专辑实体
};

} // namespace

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
    if (!album_detail_provider_ || !album_source_sync_queue_ || !parser_ || !persistence_ || source.empty()) {
        return false;
    }

    auto pending_sources = pendingAlbumDetails(std::move(source), limit);
    if (pending_sources.empty()) {
        return false;
    }

    bool saved_any = false;
    const int detail_window = workflowPageWindow(max_page_concurrency);
    for (std::size_t first = 0; first < pending_sources.size(); first += static_cast<std::size_t>(detail_window)) {
        const std::size_t last = (std::min)(pending_sources.size(), first + static_cast<std::size_t>(detail_window));
        std::vector<std::future<AlbumDetailResult>> futures;
        futures.reserve(last - first);

        for (std::size_t index = first; index < last; ++index) {
            application::model::AlbumSourceMetadata source_metadata = pending_sources[index];
            (void)album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, {});
            (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, 0, {});
            AlbumDetailResult fallback;
            fallback.source = source_metadata;

            futures.push_back(page_runner_.submit<AlbumDetailResult>(
                [this, source_metadata]() {
                    AlbumDetailResult result;
                    result.source = source_metadata;
                    auto response = album_detail_provider_->fetchAlbumDetail(source_metadata.source_album_id, source_metadata.source_res_type);
                    if (!response) {
                        return result;
                    }
                    std::vector<char> body = std::move(response->body);
                    result.fetched = true;
                    result.detail_sources = parser_->parseAlbumSources(body);
                    result.song_sources = parser_->parseSongSources(std::move(body));
                    return result;
                },
                std::move(fallback),
                "collect album detail"
            ));
        }

        for (auto &future : futures) {
            auto result = future.get();
            if (!result.fetched || result.detail_sources.empty()) {
                (void)album_source_sync_queue_->updateDetailSyncStatus(result.source.source, result.source.source_album_id, 3, "album detail fetch or parse failed");
                (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, 3, 0, "album detail fetch or parse failed");
            } else {
                const bool saved = persistence_->saveAlbumDetails(result.detail_sources, result.song_sources);
                saved_any = saved || saved_any;
                if (!saved) {
                    (void)album_source_sync_queue_->updateDetailSyncStatus(result.source.source, result.source.source_album_id, 3, "album detail save failed");
                    (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, 3, 0, "album detail save failed");
                } else {
                    const int song_count = static_cast<int>(result.song_sources.size());
                    if (song_count == 0) {
                        (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, albumSongSyncStatus(result.detail_sources, 0), 0, {});
                    } else {
                        const bool songs_saved = saved;
                        saved_any = songs_saved || saved_any;
                        if (songs_saved) {
                            (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, albumSongSyncStatus(result.detail_sources, song_count), song_count, {});
                        } else {
                            (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, 3, 0, "album detail songs save failed");
                        }
                    }
                }
            }
        }
    }

    return saved_any;
}

bool AlbumDetailBackfillWorkflow::crawlPendingAlbumSongsAndSave(std::string source, int limit, int max_page_concurrency)
{
    if (!album_detail_provider_ || !album_source_sync_queue_ || !parser_ || !persistence_ || source.empty()) {
        return false;
    }

    auto pending_sources = pendingAlbumSongs(std::move(source), limit);
    if (pending_sources.empty()) {
        return false;
    }

    bool saved_any = false;
    const int detail_window = workflowPageWindow(max_page_concurrency);
    for (std::size_t first = 0; first < pending_sources.size(); first += static_cast<std::size_t>(detail_window)) {
        const std::size_t last = (std::min)(pending_sources.size(), first + static_cast<std::size_t>(detail_window));
        std::vector<std::future<AlbumSongResult>> futures;
        futures.reserve(last - first);

        for (std::size_t index = first; index < last; ++index) {
            application::model::AlbumSourceMetadata source_metadata = pending_sources[index];
            (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, 0, {});
            AlbumSongResult fallback;
            fallback.source = source_metadata;

            futures.push_back(page_runner_.submit<AlbumSongResult>(
                [this, source_metadata]() {
                    AlbumSongResult result;
                    result.source = source_metadata;
                    auto response = album_detail_provider_->fetchAlbumDetail(source_metadata.source_album_id, source_metadata.source_res_type);
                    if (!response) {
                        return result;
                    }
                    result.fetched = true;
                    result.detail_sources = parser_->parseAlbumSources(response->body);
                    result.song_sources = parser_->parseSongSources(std::move(response->body));
                    return result;
                },
                std::move(fallback),
                "collect album songs"
            ));
        }

        for (auto &future : futures) {
            auto result = future.get();
            if (!result.fetched) {
                (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, 3, 0, "album songs fetch failed");
            } else {
                const int song_count = static_cast<int>(result.song_sources.size());
                if (song_count == 0) {
                    const bool saved = !result.detail_sources.empty() && persistence_->saveAlbumDetails(result.detail_sources, {});
                    saved_any = saved || saved_any;
                    (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, saved ? albumSongSyncStatus(result.detail_sources, 0) : 3, 0, {});
                } else {
                    const bool songs_saved = persistence_->saveAlbumDetails(result.detail_sources, result.song_sources);
                    saved_any = songs_saved || saved_any;
                    if (songs_saved) {
                        (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, albumSongSyncStatus(result.detail_sources, song_count), song_count, {});
                    } else {
                        (void)album_source_sync_queue_->updateSongsSyncStatus(result.source.source, result.source.source_album_id, 3, 0, "album songs save failed");
                    }
                }
            }
        }
    }

    return saved_any;
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
    if (!album_detail_provider_ || !album_source_sync_queue_ || !parser_ || !persistence_ || source_metadata.source_album_id.empty()) {
        return false;
    }

    (void)album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, {});
    (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, 0, {});
    auto response = album_detail_provider_->fetchAlbumDetail(source_metadata.source_album_id, source_metadata.source_res_type);
    if (!response) {
        (void)album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, "album detail fetch failed");
        (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, 0, "album detail fetch failed");
        return false;
    }

    std::vector<char> body = std::move(response->body);
    auto detail_sources = parser_->parseAlbumSources(body);
    auto song_sources = parser_->parseSongSources(std::move(body));
    if (detail_sources.empty()) {
        (void)album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, "album detail parse failed");
        (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, 0, "album detail parse failed");
        return false;
    }

    const bool saved = persistence_->saveAlbumDetails(detail_sources, song_sources);
    if (!saved) {
        (void)album_source_sync_queue_->updateDetailSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, "album detail save failed");
        (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, 0, "album detail save failed");
        return false;
    }

    const int song_count = static_cast<int>(song_sources.size());
    if (song_count == 0) {
        (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, albumSongSyncStatus(detail_sources, 0), 0, {});
        return true;
    }

    const bool songs_saved = saved;
    (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, songs_saved ? albumSongSyncStatus(detail_sources, song_count) : 3, songs_saved ? song_count : 0, songs_saved ? "" : "album detail songs save failed");
    return songs_saved;
}

bool AlbumDetailBackfillWorkflow::crawlAlbumSongsAndSave(application::model::AlbumSourceMetadata source_metadata)
{
    if (!album_detail_provider_ || !album_source_sync_queue_ || !parser_ || !persistence_ || source_metadata.source_album_id.empty()) {
        return false;
    }

    (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 1, 0, {});
    auto response = album_detail_provider_->fetchAlbumDetail(source_metadata.source_album_id, source_metadata.source_res_type);
    if (!response) {
        (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, 3, 0, "album songs fetch failed");
        return false;
    }

    auto detail_sources = parser_->parseAlbumSources(response->body);
    auto song_sources = parser_->parseSongSources(std::move(response->body));
    const int song_count = static_cast<int>(song_sources.size());
    if (song_count == 0) {
        const bool saved = !detail_sources.empty() && persistence_->saveAlbumDetails(detail_sources, {});
        (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, saved ? albumSongSyncStatus(detail_sources, 0) : 3, 0, {});
        return saved;
    }

    const bool songs_saved = persistence_->saveAlbumDetails(detail_sources, song_sources);
    (void)album_source_sync_queue_->updateSongsSyncStatus(source_metadata.source, source_metadata.source_album_id, songs_saved ? albumSongSyncStatus(detail_sources, song_count) : 3, songs_saved ? song_count : 0, songs_saved ? "" : "album songs save failed");
    return songs_saved;
}

} // namespace orchestrator::modules::crawler::application::use_cases
