#include "crawler/application/use_cases/CrawlerService.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 兼容旧构造函数，把聚合接口拆成窄 port 依赖结构。
CrawlerServiceDependencies makeDependencies(
    std::shared_ptr<application::ports::IMusicProvider> provider,
    std::shared_ptr<application::ports::FileStore> file_store,
    std::shared_ptr<application::ports::SongRepository> song_repository,
    std::shared_ptr<application::ports::AlbumRepository> album_repository,
    std::shared_ptr<application::ports::SingerRepository> singer_repository,
    std::shared_ptr<application::ports::MetadataParser> parser,
    std::shared_ptr<application::ports::TaskExecutor> parse_executor,
    std::shared_ptr<application::ports::TaskExecutor> batch_executor,
    std::shared_ptr<application::ports::TaskExecutor> page_executor,
    std::shared_ptr<application::ports::TaskExecutor> persistence_executor,
    int max_batch_concurrency,
    int max_page_concurrency,
    int max_retries,
    std::chrono::milliseconds task_timeout,
    application::model::CrawlerRuntimeSettings runtime_settings,
    std::shared_ptr<application::ports::SongSourceRepository> song_source_repository,
    std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository,
    std::shared_ptr<application::ports::AlbumSourceRepository> album_source_repository
)
{
    CrawlerServiceDependencies dependencies;
    dependencies.providers.raw_page_fetcher = provider;
    dependencies.providers.song_search_provider = provider;
    dependencies.providers.album_search_provider = provider;
    dependencies.providers.singer_catalog_provider = provider;
    dependencies.providers.album_detail_provider = provider;

    dependencies.parsers.parse_result_parser = parser;
    dependencies.parsers.song_source_parser = parser;
    dependencies.parsers.singer_source_parser = parser;
    dependencies.parsers.album_source_parser = parser;
    dependencies.parsers.singer_profile_parser = parser;
    dependencies.parsers.page_inspector = parser;

    dependencies.persistence.file_store = std::move(file_store);
    dependencies.persistence.song_repository = std::move(song_repository);
    dependencies.persistence.album_repository = std::move(album_repository);
    dependencies.persistence.singer_repository = std::move(singer_repository);
    dependencies.persistence.song_source_repository = std::move(song_source_repository);
    dependencies.persistence.singer_source_repository = std::move(singer_source_repository);
    dependencies.persistence.album_source_writer = album_source_repository;
    dependencies.persistence.album_source_sync_queue = std::move(album_source_repository);

    dependencies.executors.parse_executor = std::move(parse_executor);
    dependencies.executors.batch_executor = std::move(batch_executor);
    dependencies.executors.page_executor = std::move(page_executor);
    dependencies.executors.persistence_executor = std::move(persistence_executor);

    dependencies.options.max_batch_concurrency = max_batch_concurrency;
    dependencies.options.max_page_concurrency = max_page_concurrency;
    dependencies.options.max_retries = max_retries;
    dependencies.options.task_timeout = task_timeout;
    dependencies.options.runtime_settings = std::move(runtime_settings);
    dependencies.diagnostics = std::make_shared<application::ports::NullDiagnosticsSink>();
    return dependencies;
}

} // namespace

CrawlerService::CrawlerService(
    std::shared_ptr<application::ports::IMusicProvider> provider,
    std::shared_ptr<application::ports::FileStore> file_store,
    std::shared_ptr<application::ports::SongRepository> song_repository,
    std::shared_ptr<application::ports::AlbumRepository> album_repository,
    std::shared_ptr<application::ports::SingerRepository> singer_repository,
    std::shared_ptr<application::ports::MetadataParser> parser,
    std::shared_ptr<application::ports::TaskExecutor> parse_executor,
    std::shared_ptr<application::ports::TaskExecutor> batch_executor,
    std::shared_ptr<application::ports::TaskExecutor> page_executor,
    std::shared_ptr<application::ports::TaskExecutor> persistence_executor,
    int max_batch_concurrency,
    int max_page_concurrency,
    int max_retries,
    std::chrono::milliseconds task_timeout,
    application::model::CrawlerRuntimeSettings runtime_settings,
    std::shared_ptr<application::ports::SongSourceRepository> song_source_repository,
    std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository,
    std::shared_ptr<application::ports::AlbumSourceRepository> album_source_repository
) : CrawlerService(makeDependencies(
        std::move(provider),
        std::move(file_store),
        std::move(song_repository),
        std::move(album_repository),
        std::move(singer_repository),
        std::move(parser),
        std::move(parse_executor),
        std::move(batch_executor),
        std::move(page_executor),
        std::move(persistence_executor),
        max_batch_concurrency,
        max_page_concurrency,
        max_retries,
        task_timeout,
        std::move(runtime_settings),
        std::move(song_source_repository),
        std::move(singer_source_repository),
        std::move(album_source_repository)
    ))
{}

} // namespace orchestrator::modules::crawler::application::use_cases
