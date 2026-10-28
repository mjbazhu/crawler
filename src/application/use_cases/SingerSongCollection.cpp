#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "SingerCatalogPagingSupport.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace singer_detail; // 歌手目录入口共享的分页与完成度规则

bool SingerCatalogWorkflow::crawlSingerSongsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency)
{
    const auto result = crawlSingerSongsAndSaveResult(singer_id, limit, max_page_concurrency);
    return result.written_count > 0 && (result.success || result.partial);
}

application::model::SingerCollectionResult SingerCatalogWorkflow::crawlSingerSongsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total, std::optional<int> source_album_total, bool refresh_profile)
{
    if (!singer_catalog_provider_ || !parser_ || !persistence_ || singer_id == 0) {
        return {false, false, 0, "singer song workflow is not configured"};
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
    const auto page_size = singer_catalog_provider_->singerSongPageSize();
    std::size_t stored_count = 0;
    bool stop_paging = false;
    bool fetch_failed = false;
    bool save_failed = false;

    for (int first_page = 1; first_page <= kWorkflowMaxPages && !stop_paging && !reachedWorkflowLimit(stored_count, normalized_limit);) {
        const int window = singerPageWindow(max_page_concurrency, stored_count, normalized_limit, source_song_total, page_size);
        const int last_page = (std::min)(kWorkflowMaxPages, first_page + window - 1);
        auto pages = collectOrderedPageWindow<SingerSongPageResult>(
            page_runner_,
            first_page,
            last_page,
            [this, singer_id](int page) {
                SingerSongPageResult page_result;
                page_result.page = page;
                auto response = singer_catalog_provider_->fetchSingerSongsPage(singer_id, page);
                if (!response || response->status_code < 200 || response->status_code >= 300) {
                    return page_result;
                }

                page_result.fetched = true;
                std::vector<char> body = std::move(response->body);
                page_result.page_info = parser_->inspectSingerSongPage(body);
                page_result.batch = parser_->parseSources(std::move(body));
                markSeedSinger(page_result.batch.songs, singer_id);
                return page_result;
            },
            "collect singer song page"
        );

        bool should_stop = false;
        for (auto &page_result : pages) {
            if (!page_result.fetched) {
                fetch_failed = true;
                should_stop = true;
            } else {
                const auto page_count = page_result.batch.songs.size();
                const std::size_t remaining = normalized_limit == 0 ? page_result.batch.songs.size() : static_cast<std::size_t>(normalized_limit) - stored_count;
                if (!page_result.batch.songs.empty() && remaining > 0) {
                    const auto selected_count = (std::min)(remaining, page_result.batch.songs.size());
                    auto batch = std::move(page_result.batch);
                    batch.songs.resize(selected_count);
                    // 当前入口只选择这些歌曲，不跟随推荐专辑卡片扩展采集范围。
                    std::erase_if(batch.albums, [&](const auto &album) {
                        return std::none_of(batch.songs.begin(), batch.songs.end(), [&](const auto &song) { return song.source == album.source && (song.source_album_id == album.source_album_id || song.source_album_id == album.source_material_id); });
                    });
                    if (profile) batch.profiles.push_back(*profile);
                    if (completion_) {
                        const auto completed = completion_->completeAlbums(batch);
                        if (completed.isSuccess())
                            batch = *completed.data();
                        else
                            completion_error = completed.message();
                    }
                    const bool saved = persistence_->hasAtomicWriter() ? persistence_->saveSources(std::move(batch), 0) : persistence_->saveSongSources(batch.songs, static_cast<int>(selected_count));
                    if (saved) {
                        profile.reset();
                        stored_count += selected_count;
                    } else {
                        save_failed = true;
                        should_stop = true;
                    }
                }

                if (shouldStopSingerPage(page_result.page_info.should_stop_paging, page_count, stored_count, source_song_total) || reachedWorkflowLimit(stored_count, normalized_limit)) {
                    should_stop = true;
                }
            }
        }

        if (should_stop) {
            stop_paging = true;
        }
        if (last_page == kWorkflowMaxPages && !stop_paging && !reachedWorkflowLimit(stored_count, normalized_limit) && completion_error.empty()) {
            completion_error = "singer song pagination limit reached";
        }
        first_page = last_page + 1;
    }

    application::model::SingerCollectionResult result;
    result.written_count = static_cast<int>(stored_count);
    result.partial = stored_count > 0 && (fetch_failed || save_failed || !completion_error.empty());
    result.success = !fetch_failed && !save_failed && completion_error.empty();
    if (fetch_failed) {
        result.error = "failed to fetch singer song page";
    } else if (save_failed) {
        result.error = "failed to save singer song sources";
    } else if (!completion_error.empty())
        result.error = completion_error;
    result.source_song_total = source_song_total;
    result.source_album_total = source_album_total;
    applyDeclaredTotalCompletion(result, source_song_total, "singer songs");
    return result;
}

} // namespace orchestrator::modules::crawler::application::use_cases
