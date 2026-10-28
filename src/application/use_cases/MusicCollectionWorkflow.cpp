#include "crawler/application/use_cases/MusicCollectionWorkflow.h"

#include "crawler/application/use_cases/ParserDispatch.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

MusicCollectionWorkflow::MusicCollectionWorkflow(std::shared_ptr<UrlIngestionWorkflow> url, std::shared_ptr<SearchCollectionWorkflow> search, std::shared_ptr<SingerCatalogWorkflow> singer, std::shared_ptr<AlbumDetailBackfillWorkflow> album)
    : url_workflow_(std::move(url)), search_workflow_(std::move(search)), singer_workflow_(std::move(singer)), album_detail_workflow_(std::move(album))
{}

MusicCollectionWorkflow::MusicCollectionWorkflow(
    std::shared_ptr<application::ports::RawPageFetcher> raw_page_fetcher,
    std::shared_ptr<application::ports::SongSearchProvider> song_search_provider,
    std::shared_ptr<application::ports::AlbumSearchProvider> album_search_provider,
    std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider,
    std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider,
    std::shared_ptr<application::ports::ParseResultParser> parse_result_parser,
    std::shared_ptr<application::ports::SongSourceParser> song_source_parser,
    std::shared_ptr<application::ports::SingerSourceParser> singer_source_parser,
    std::shared_ptr<application::ports::AlbumSourceParser> album_source_parser,
    std::shared_ptr<application::ports::SingerProfileParser> singer_profile_parser,
    std::shared_ptr<application::ports::PageInspector> page_inspector,
    std::shared_ptr<application::ports::AlbumSourceSyncQueue> album_source_sync_queue,
    std::shared_ptr<MetadataPersistenceService> persistence,
    std::shared_ptr<application::ports::TaskExecutor> parse_executor,
    std::shared_ptr<application::ports::TaskExecutor> page_executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
)
{
    auto parser = std::make_shared<ParserDispatch>(
        std::move(parse_result_parser),
        std::move(song_source_parser),
        std::move(singer_source_parser),
        std::move(album_source_parser),
        std::move(singer_profile_parser),
        std::move(page_inspector),
        std::move(parse_executor),
        diagnostics
    );

    url_workflow_ = std::make_shared<UrlIngestionWorkflow>(raw_page_fetcher, parser, persistence);
    search_workflow_ = std::make_shared<SearchCollectionWorkflow>(
        std::move(song_search_provider),
        std::move(album_search_provider),
        album_detail_provider,
        parser,
        persistence,
        page_executor,
        diagnostics
    );
    singer_workflow_ = std::make_shared<SingerCatalogWorkflow>(
        std::move(singer_catalog_provider),
        parser,
        persistence,
        page_executor,
        diagnostics
    );
    album_detail_workflow_ = std::make_shared<AlbumDetailBackfillWorkflow>(
        std::move(album_detail_provider),
        std::move(album_source_sync_queue),
        std::move(parser),
        std::move(persistence),
        std::move(page_executor),
        std::move(diagnostics)
    );
}

bool MusicCollectionWorkflow::processUrl(std::string url, int limit)
{
    return url_workflow_ ? url_workflow_->processUrl(std::move(url), limit) : false;
}

bool MusicCollectionWorkflow::searchAndSave(std::string keyword, int limit, int max_page_concurrency)
{
    return search_workflow_ ? search_workflow_->searchAndSave(std::move(keyword), limit, max_page_concurrency) : false;
}

bool MusicCollectionWorkflow::crawlSingerSongsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency)
{
    return singer_workflow_ ? singer_workflow_->crawlSingerSongsAndSave(singer_id, limit, max_page_concurrency) : false;
}

application::model::SingerCollectionResult MusicCollectionWorkflow::crawlSingerSongsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total, std::optional<int> source_album_total, bool refresh_profile)
{
    return singer_workflow_ ? singer_workflow_->crawlSingerSongsAndSaveResult(singer_id, limit, max_page_concurrency, source_song_total, source_album_total, refresh_profile) : application::model::SingerCollectionResult {false, false, 0, "singer workflow is not configured"};
}

bool MusicCollectionWorkflow::crawlSingerIndexAndSave(std::string tab, int limit)
{
    return singer_workflow_ ? singer_workflow_->crawlSingerIndexAndSave(std::move(tab), limit) : false;
}

application::model::SingerCollectionResult MusicCollectionWorkflow::crawlSingerIndexAndSaveResult(std::string tab, int limit)
{
    return singer_workflow_ ? singer_workflow_->crawlSingerIndexAndSaveResult(std::move(tab), limit) : application::model::SingerCollectionResult {false, false, 0, "singer workflow is not configured"};
}

bool MusicCollectionWorkflow::crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency)
{
    return singer_workflow_ ? singer_workflow_->crawlSingerAlbumsAndSave(singer_id, limit, max_page_concurrency) : false;
}

application::model::SingerCollectionResult MusicCollectionWorkflow::crawlSingerAlbumsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total, std::optional<int> source_album_total, bool refresh_profile)
{
    return singer_workflow_ ? singer_workflow_->crawlSingerAlbumsAndSaveResult(singer_id, limit, max_page_concurrency, source_song_total, source_album_total, refresh_profile) : application::model::SingerCollectionResult {false, false, 0, "singer workflow is not configured"};
}

application::model::SingerSourceProfile MusicCollectionWorkflow::fetchAndSaveSingerSourceProfile(std::uint64_t singer_id)
{
    return singer_workflow_ ? singer_workflow_->fetchAndSaveSingerSourceProfile(singer_id) : application::model::SingerSourceProfile {};
}

bool MusicCollectionWorkflow::crawlPendingAlbumDetailsAndSave(std::string source, int limit, int max_page_concurrency)
{
    return album_detail_workflow_ ? album_detail_workflow_->crawlPendingAlbumDetailsAndSave(std::move(source), limit, max_page_concurrency) : false;
}

bool MusicCollectionWorkflow::crawlPendingAlbumSongsAndSave(std::string source, int limit, int max_page_concurrency)
{
    return album_detail_workflow_ ? album_detail_workflow_->crawlPendingAlbumSongsAndSave(std::move(source), limit, max_page_concurrency) : false;
}

std::vector<application::model::AlbumSourceMetadata> MusicCollectionWorkflow::pendingAlbumDetails(std::string source, int limit)
{
    return album_detail_workflow_ ? album_detail_workflow_->pendingAlbumDetails(std::move(source), limit) : std::vector<application::model::AlbumSourceMetadata> {};
}

std::vector<application::model::AlbumSourceMetadata> MusicCollectionWorkflow::pendingAlbumSongs(std::string source, int limit)
{
    return album_detail_workflow_ ? album_detail_workflow_->pendingAlbumSongs(std::move(source), limit) : std::vector<application::model::AlbumSourceMetadata> {};
}

bool MusicCollectionWorkflow::crawlAlbumDetailAndSave(application::model::AlbumSourceMetadata source_metadata)
{
    return album_detail_workflow_ ? album_detail_workflow_->crawlAlbumDetailAndSave(std::move(source_metadata)) : false;
}

bool MusicCollectionWorkflow::crawlAlbumSongsAndSave(application::model::AlbumSourceMetadata source_metadata)
{
    return album_detail_workflow_ ? album_detail_workflow_->crawlAlbumSongsAndSave(std::move(source_metadata)) : false;
}

} // namespace orchestrator::modules::crawler::application::use_cases
