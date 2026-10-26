#include "crawler/application/use_cases/SingerCatalogWorkflow.h"

#include "crawler/application/use_cases/WorkflowPaging.h"

#include <algorithm>
#include <cstdint>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 歌手歌曲分页采集结果。
struct SingerSongPageResult {
    int page = 0;                                                     // 页码
    std::vector<application::model::SongSourceMetadata> song_sources; // 本页歌曲来源
    application::ports::SingerSongPageInfo page_info;                 // 分页摘要
    bool fetched = false;                                             // 是否成功获取响应
};

// 歌手专辑分页采集结果。
struct SingerAlbumPageResult {
    int page = 0;                                                       // 页码
    std::vector<application::model::AlbumSourceMetadata> album_sources; // 本页专辑来源
    application::ports::SingerAlbumPageInfo page_info;                  // 分页摘要
    bool fetched = false;                                               // 是否成功获取响应
};

// 将本次按歌手入口采集的歌手标记到歌曲歌手关系里。
void markSeedSinger(std::vector<application::model::SongSourceMetadata> &songs, std::uint64_t singer_id)
{
    const std::string seed_source_singer_id = std::to_string(singer_id);
    for (auto &song : songs) {
        bool matched = false;
        bool has_primary = false;
        std::uint32_t max_artist_order = 0;
        for (auto &artist_ref : song.artist_refs) {
            has_primary = has_primary || artist_ref.is_primary;
            max_artist_order = (std::max)(max_artist_order, artist_ref.artist_order);
            if (artist_ref.source_artist_id == seed_source_singer_id) {
                artist_ref.is_seed = true;
                matched = true;
            }
        }

        if (!matched) {
            application::model::SongSourceArtistRef seed_ref;
            seed_ref.source_artist_id = seed_source_singer_id;
            if (song.source_primary_artist_id.empty() || song.source_primary_artist_id == seed_source_singer_id) {
                seed_ref.source_artist_name = song.source_primary_artist_name;
            }
            seed_ref.artist_order = max_artist_order + 1;
            seed_ref.is_primary = !has_primary;
            seed_ref.is_seed = true;
            song.artist_refs.push_back(std::move(seed_ref));
        }
    }
}

// 计算歌手分类本次会写入的唯一平台来源数量，避免接口重复项干扰批次进度。
int singerIndexUniqueCount(const std::vector<application::model::SingerSourceMetadata> &singers, int limit)
{
    const int effective_limit = limit < 0 ? kWorkflowDefaultLimit : limit;
    const std::size_t max_count = effective_limit == 0 ? singers.size() : (std::min)(singers.size(), static_cast<std::size_t>(effective_limit));
    std::set<std::string> unique_keys;
    for (std::size_t index = 0; index < max_count; ++index) {
        const auto &singer = singers[index];
        if (!singer.source.empty() && !singer.source_singer_id.empty() && !singer.name.empty()) {
            unique_keys.insert(singer.source + "\n" + singer.source_singer_id);
        }
    }
    return static_cast<int>(unique_keys.size());
}

// 歌手歌曲/专辑采集 limit=0 表示分页保护范围内全部获取，正数仍限制到 500。
int normalizeSingerIdsWorkflowLimit(int limit)
{
    if (limit < 0) {
        return kWorkflowDefaultLimit;
    }
    if (limit == 0) {
        return 0;
    }
    return (std::min)(limit, 500);
}

// 判断歌手概览是否包含可保存的声明总数。
bool hasSingerSourceProfileCounts(const application::model::SingerSourceProfile &profile)
{
    return profile.source_song_total || profile.source_album_total;
}

// 平台总数已知时，把受 limit 截断的成功结果暴露为部分完成。
void applyDeclaredTotalCompletion(application::model::SingerCollectionResult &result, std::optional<int> declared_total, std::string_view item_name)
{
    if (!result.success || !declared_total || *declared_total <= result.written_count) {
        return;
    }

    result.success = false;
    result.partial = result.written_count > 0;
    result.error = std::string(item_name) + " incomplete: fetched " + std::to_string(result.written_count) + " of " + std::to_string(*declared_total);
}

} // namespace

SingerCatalogWorkflow::SingerCatalogWorkflow(
    std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider,
    std::shared_ptr<ParserDispatch> parser,
    std::shared_ptr<MetadataPersistenceService> persistence,
    std::shared_ptr<application::ports::TaskExecutor> page_executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
) : singer_catalog_provider_(std::move(singer_catalog_provider)),
    parser_(std::move(parser)),
    persistence_(std::move(persistence)),
    page_runner_(std::move(page_executor), std::move(diagnostics))
{}

bool SingerCatalogWorkflow::crawlSingerSongsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency)
{
    const auto result = crawlSingerSongsAndSaveResult(singer_id, limit, max_page_concurrency);
    return result.written_count > 0 && (result.success || result.partial);
}

application::model::SingerCollectionResult SingerCatalogWorkflow::crawlSingerSongsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total, std::optional<int> source_album_total)
{
    if (!singer_catalog_provider_ || !parser_ || !persistence_ || singer_id == 0) {
        return {false, false, 0, "singer song workflow is not configured"};
    }

    const int normalized_limit = normalizeSingerIdsWorkflowLimit(limit);
    const int window = workflowPageWindow(max_page_concurrency);
    std::size_t stored_count = 0;
    bool stop_paging = false;
    bool fetch_failed = false;
    bool save_failed = false;

    for (int first_page = 1; first_page <= kWorkflowMaxPages && !stop_paging && !reachedWorkflowLimit(stored_count, normalized_limit); first_page += window) {
        const int last_page = (std::min)(kWorkflowMaxPages, first_page + window - 1);
        auto pages = collectOrderedPageWindow<SingerSongPageResult>(
            page_runner_,
            first_page,
            last_page,
            [this, singer_id](int page) {
                SingerSongPageResult page_result;
                page_result.page = page;
                auto response = singer_catalog_provider_->fetchSingerSongsPage(singer_id, page);
                if (!response) {
                    return page_result;
                }

                page_result.fetched = true;
                std::vector<char> body = std::move(response->body);
                page_result.page_info = parser_->inspectSingerSongPage(body);
                page_result.song_sources = parser_->parseSongSources(std::move(body));
                markSeedSinger(page_result.song_sources, singer_id);
                return page_result;
            },
            "collect singer song page"
        );

        bool should_stop = false;
        for (const auto &page_result : pages) {
            if (!page_result.fetched) {
                fetch_failed = true;
                should_stop = true;
            } else {
                const std::size_t remaining = normalized_limit == 0 ? page_result.song_sources.size() : static_cast<std::size_t>(normalized_limit) - stored_count;
                if (!page_result.song_sources.empty() && remaining > 0) {
                    const int save_limit = normalized_limit == 0 ? 0 : static_cast<int>(remaining);
                    const bool saved = persistence_->saveSongSources(page_result.song_sources, save_limit);
                    if (saved) {
                        stored_count += (std::min)(remaining, page_result.song_sources.size());
                    } else {
                        save_failed = true;
                        should_stop = true;
                    }
                }

                if (page_result.page_info.should_stop_paging || reachedWorkflowLimit(stored_count, normalized_limit)) {
                    should_stop = true;
                }
            }
        }

        if (should_stop) {
            stop_paging = true;
        }
    }

    application::model::SingerCollectionResult result;
    result.written_count = static_cast<int>(stored_count);
    result.partial = stored_count > 0 && (fetch_failed || save_failed);
    result.success = !fetch_failed && !save_failed;
    if (fetch_failed) {
        result.error = "failed to fetch singer song page";
    } else if (save_failed) {
        result.error = "failed to save singer song sources";
    }
    result.source_song_total = source_song_total;
    result.source_album_total = source_album_total;
    applyDeclaredTotalCompletion(result, source_song_total, "singer songs");
    return result;
}

bool SingerCatalogWorkflow::crawlSingerIndexAndSave(std::string tab, int limit)
{
    const auto result = crawlSingerIndexAndSaveResult(std::move(tab), limit);
    return result.success && result.written_count > 0;
}

application::model::SingerCollectionResult SingerCatalogWorkflow::crawlSingerIndexAndSaveResult(std::string tab, int limit)
{
    if (!singer_catalog_provider_ || !parser_ || !persistence_ || tab.empty()) {
        return {false, false, 0, "singer index workflow is not configured"};
    }

    auto response = singer_catalog_provider_->fetchSingerIndexPage(tab);
    if (!response) {
        return {false, false, 0, "failed to fetch singer index page"};
    }

    auto singer_sources = parser_->parseSingerSources(std::move(response->body), tab);
    const int singer_index_limit = limit < 0 ? kWorkflowDefaultLimit : limit;
    const int unique_count = singerIndexUniqueCount(singer_sources, singer_index_limit);
    if (unique_count == 0) {
        return {false, false, 0, "singer index page did not contain valid singer sources"};
    }

    const bool saved = persistence_->saveSingerSources(singer_sources, singer_index_limit);
    if (!saved) {
        return {false, false, unique_count, "failed to save singer sources"};
    }
    return {true, false, unique_count, {}};
}

bool SingerCatalogWorkflow::crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency)
{
    const auto result = crawlSingerAlbumsAndSaveResult(singer_id, limit, max_page_concurrency);
    return result.written_count > 0 && (result.success || result.partial);
}

application::model::SingerCollectionResult SingerCatalogWorkflow::crawlSingerAlbumsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total, std::optional<int> source_album_total)
{
    if (!singer_catalog_provider_ || !parser_ || !persistence_ || singer_id == 0) {
        return {false, false, 0, "singer album workflow is not configured"};
    }

    const int normalized_limit = normalizeSingerIdsWorkflowLimit(limit);
    const int window = workflowPageWindow(max_page_concurrency);
    const std::string source_artist_id = std::to_string(singer_id);
    std::vector<application::model::AlbumSourceMetadata> collected;
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
                if (!response) {
                    return page_result;
                }

                page_result.fetched = true;
                std::vector<char> body = std::move(response->body);
                page_result.page_info = parser_->inspectSingerAlbumPage(body);
                page_result.album_sources = parser_->parseAlbumSources(std::move(body));
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
                for (auto &album : page_result.album_sources) {
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
                            entry_artist.artist_order = static_cast<std::uint32_t>(album.artist_refs.size()) + 1;
                            entry_artist.is_seed = true;
                            album.artist_refs.push_back(std::move(entry_artist));
                        }
                        collected.push_back(std::move(album));
                    }
                }

                if (page_result.page_info.should_stop_paging || reachedWorkflowLimit(collected.size(), normalized_limit)) {
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

    if (collected.empty()) {
        result.success = true;
        applyDeclaredTotalCompletion(result, source_album_total, "singer albums");
        return result;
    }

    if (!persistence_->saveAlbumSources(collected, normalized_limit)) {
        result.success = false;
        result.partial = false;
        result.error = "failed to save singer album sources";
        result.written_count = 0;
        return result;
    }

    result.success = !fetch_failed;
    applyDeclaredTotalCompletion(result, source_album_total, "singer albums");
    return result;
}

application::model::SingerSourceProfile SingerCatalogWorkflow::fetchAndSaveSingerSourceProfile(std::uint64_t singer_id)
{
    application::model::SingerSourceProfile profile;
    profile.source = "migu";
    profile.source_singer_id = std::to_string(singer_id);

    if (!singer_catalog_provider_ || !parser_) {
        return profile;
    }

    auto response = singer_catalog_provider_->fetchSingerInfo(singer_id);
    if (!response) {
        return profile;
    }

    profile = parser_->parseSingerSourceProfile(std::move(response->body));
    if (profile.source.empty()) {
        profile.source = "migu";
    }
    if (profile.source_singer_id.empty()) {
        profile.source_singer_id = std::to_string(singer_id);
    }
    if (persistence_ && hasSingerSourceProfileCounts(profile)) {
        (void)persistence_->saveSingerSourceProfile(profile);
    }
    return profile;
}

} // namespace orchestrator::modules::crawler::application::use_cases
