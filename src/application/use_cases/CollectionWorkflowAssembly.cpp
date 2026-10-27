#include "CollectionWorkflowAssembly.h"

#include "crawler/application/use_cases/CatalogMetadataCompletion.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

std::shared_ptr<MusicCollectionWorkflow> assembleCollectionWorkflows(const CrawlerServiceDependencies &dependencies, std::shared_ptr<MetadataPersistenceService> persistence, std::shared_ptr<application::ports::DiagnosticsSink> diagnostics)
{
    const auto &providers = dependencies.providers;
    const auto &parsers = dependencies.parsers;
    const auto &executors = dependencies.executors;
    auto parser = std::make_shared<ParserDispatch>(parsers.parse_result_parser, parsers.song_source_parser, parsers.singer_source_parser, parsers.album_source_parser, parsers.singer_profile_parser, parsers.page_inspector, executors.parse_executor, diagnostics);
    std::shared_ptr<CatalogMetadataCompletion> completion;
    if (dependencies.persistence.metadata_batch_writer && dependencies.persistence.catalog_query_repository) {
        completion = std::make_shared<CatalogMetadataCompletion>(CatalogMetadataCompletionDependencies {providers.singer_catalog_provider, providers.album_detail_provider, dependencies.persistence.catalog_query_repository, parser});
    }
    auto url = std::make_shared<UrlIngestionWorkflow>(providers.raw_page_fetcher, parser, persistence);
    auto search = std::make_shared<SearchCollectionWorkflow>(providers.song_search_provider, providers.album_search_provider, providers.album_detail_provider, parser, persistence, executors.page_executor, diagnostics);
    auto singer = std::make_shared<SingerCatalogWorkflow>(SingerCatalogWorkflowDependencies {providers.singer_catalog_provider, parser, persistence, executors.page_executor, diagnostics, completion});
    auto album = std::make_shared<AlbumDetailBackfillWorkflow>(providers.album_detail_provider, dependencies.persistence.album_source_sync_queue, parser, persistence, executors.page_executor, diagnostics);
    return std::make_shared<MusicCollectionWorkflow>(std::move(url), std::move(search), std::move(singer), std::move(album));
}

} // namespace orchestrator::modules::crawler::application::use_cases
