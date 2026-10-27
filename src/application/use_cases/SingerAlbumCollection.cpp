#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "SingerCatalogPagingSupport.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace singer_detail; // 歌手目录入口共享的分页与完成度规则

bool SingerCatalogWorkflow::crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency)
{
    const auto result = crawlSingerAlbumsAndSaveResult(singer_id, limit, max_page_concurrency);
    return result.written_count > 0 && (result.success || result.partial);
}

application::model::SingerCollectionResult SingerCatalogWorkflow::crawlSingerAlbumsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total, std::optional<int> source_album_total, bool refresh_profile)
{
    if (!singer_catalog_provider_ || !parser_ || !persistence_ || singer_id == 0) {
        return {false, false, 0, "singer album workflow is not configured"};
    }

    std::optional<application::model::SingerSourceProfile> profile;
    std::string completion_error;
    if (completion_) {
        const auto context = completion_->singerContext(singer_id, source_song_total, source_album_total, refresh_profile);
        if (context.isSuccess()) {
            profile = *context.data();
            source_song_total = profile->source_song_total;
            source_album_total = profile->source_album_total;
        } else
            completion_error = context.message();
    }

    const int normalized_limit = normalizeSingerIdsWorkflowLimit(limit);
    const int window = workflowPageWindow(max_page_concurrency);
    const std::string source_artist_id = std::to_string(singer_id);
    application::model::MetadataWriteBatch batch;
    auto &collected = batch.albums;
    collected.reserve(static_cast<std::size_t>(normalized_limit));
    bool stop_paging = false;
    bool fetch_failed = false;

    for (int first_page = 1; first_page <= kWorkflowMaxPages && !stop_paging && !reachedWorkflowLimit(collected.size(), normalized_limit); first_page += window) {
        const int last_page = (std::min)(kWorkflowMaxPages, first_page + window - 1);
        auto pages = collectOrderedPageWindow<SingerAlbumPageResult>(
            page_runner_,
            first_page,
            last_page,
            [this, singer_id](int page) {
                SingerAlbumPageResult page_result;
                page_result.page = page;
                auto response = singer_catalog_provider_->fetchSingerAlbumsPage(singer_id, page);
                if (!response || response->status_code < 200 || response->status_code >= 300) {
                    return page_result;
                }

                page_result.fetched = true;
                std::vector<char> body = std::move(response->body);
                page_result.page_info = parser_->inspectSingerAlbumPage(body);
                page_result.batch = parser_->parseSources(std::move(body));
                return page_result;
            },
            "collect singer album page"
        );

        bool should_stop = false;
        for (auto &page_result : pages) {
            if (!page_result.fetched) {
                fetch_failed = true;
                should_stop = true;
            } else {
                batch.singers.insert(batch.singers.end(), page_result.batch.singers.begin(), page_result.batch.singers.end());
                batch.profiles.insert(batch.profiles.end(), page_result.batch.profiles.begin(), page_result.batch.profiles.end());
                for (auto &album : page_result.batch.albums) {
                    if (!reachedWorkflowLimit(collected.size(), normalized_limit)) {
                        if (album.source_artist_id.empty()) {
                            album.source_artist_id = source_artist_id;
                        }
                        auto seed = std::find_if(album.artist_refs.begin(), album.artist_refs.end(), [&](const auto &artist) { return artist.source_artist_id == source_artist_id; });
                        if (seed != album.artist_refs.end()) {
                            seed->is_seed = true;
                        } else {
                            application::model::SongSourceArtistRef entry_artist;
                            entry_artist.source_artist_id = source_artist_id;
                            if (album.source_artist_id == source_artist_id) entry_artist.source_artist_name = album.source_artist_name;
                            // 入口只证明收录关系，不能推断专辑署名顺序；零值不覆盖详情顺序。
                            entry_artist.artist_order = 0;
                            entry_artist.is_seed = true;
                            album.artist_refs.push_back(std::move(entry_artist));
                        }
                        collected.push_back(std::move(album));
                    }
                }

                if (shouldStopSingerPage(page_result.page_info.should_stop_paging, page_result.batch.albums.size(), collected.size(), source_album_total) || reachedWorkflowLimit(collected.size(), normalized_limit)) {
                    should_stop = true;
                }
            }
        }

        if (should_stop) {
            stop_paging = true;
        }
    }

    application::model::SingerCollectionResult result;
    result.written_count = static_cast<int>(collected.size());
    result.source_song_total = source_song_total;
    result.source_album_total = source_album_total;
    result.partial = !collected.empty() && fetch_failed;
    if (fetch_failed) {
        result.error = "failed to fetch singer album page";
        if (collected.empty()) {
            return result;
        }
    }

    if (profile) batch.profiles.push_back(*profile);
    if (completion_) {
        const auto completed = completion_->completeAlbums(batch);
        if (completed.isSuccess())
            batch = *completed.data();
        else
            completion_error = completed.message();
    }
    const bool empty = batch.albums.empty() && batch.singers.empty() && batch.profiles.empty();
    const bool saved = empty || (persistence_->hasAtomicWriter() ? persistence_->saveSources(std::move(batch), 0) : persistence_->saveAlbumSources(collected, normalized_limit));
    if (!saved) {
        result.success = false;
        result.partial = false;
        result.error = "failed to save singer album sources";
        result.written_count = 0;
        return result;
    }

    result.success = !fetch_failed && completion_error.empty();
    result.partial = result.written_count > 0 && !result.success;
    if (!completion_error.empty()) result.error = completion_error;
    applyDeclaredTotalCompletion(result, source_album_total, "singer albums");
    return result;
}

} // namespace orchestrator::modules::crawler::application::use_cases
