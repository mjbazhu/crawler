#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/app/ResourceTransferComposition.h"
#include "crawler/infrastructure/app/ResourcePipelineConfiguration.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferQueries.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceTransfer.h"
#include "crawler/application/use_cases/LegacyResourceTransferService.h"
#include "crawler/api/routing/ResourceTransferRoutes.h"
#include "crawler/api/routing/ResourceParallelLegacyRouteSink.h"

#include "crawler/infrastructure/music/migu/provider/MiguRequestFactory.h"
#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/api/routing/CrawlerRoutes.h"
#include "crawler/api/routing/ResourceWriteRouteSink.h"
#include "crawler/application/ports/parsing/MetadataParser.h"
#include "crawler/application/ports/provider/IMusicProvider.h"
#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/application/use_cases/MiguPacmtokenService.h"
#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"
#include "crawler/infrastructure/app/LyricProcessingComposition.h"
#include "crawler/infrastructure/app/MetadataCoverComposition.h"
#include "crawler/infrastructure/app/CloudFileStoreComposition.h"
#include "crawler/infrastructure/app/PersistenceAssembly.h"
#include "crawler/infrastructure/app/ResourceTransferFactory.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceWriteInspector.h"
#include "crawler/infrastructure/app/RuntimeSettingsSnapshotFactory.h"
#include "crawler/infrastructure/execution/asio/AsioExecutionContext.h"
#include "crawler/infrastructure/execution/asio/AsioAsyncRuntime.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsyncHttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/http/server/DrogonHttpServer.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferAccess.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguPacmtokenClient.h"
#include "crawler/infrastructure/music/migu/provider/MiguAsyncSongResourceProvider.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/music/migu/provider/MiguRuntimePacmtokenStore.h"
#include "crawler/infrastructure/observability/logging/ConsoleDiagnosticsSink.h"
#include "crawler/infrastructure/observability/logging/Logger.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"
#include "crawler/infrastructure/storage/rclone/RcloneRemoteCopier.h"

#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <chrono>
#include <cstddef>
#include <cctype>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::app {

namespace {

namespace logging = orchestrator::modules::crawler::infrastructure::observability::logging;

// 记录组合根生命周期日志，避免裸 stdout/stderr 破坏日志格式。
void logApp(logging::LogLevel level, std::string_view message)
{
    logging::Logger::instance().log(level, "App", message);
}

// 简单的 URL 判断函数
bool isUrl(std::string_view value)
{
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

// 转为小写 ASCII，便于解析后端开关。
std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isRcloneBackend(std::string backend)
{
    return lowerAscii(std::move(backend)) == "rclone";
}

} // namespace

class ServerApplication::Impl {
public:
    explicit Impl(const Config &config) : config_(config)
    {}

    Config config_;

    std::shared_ptr<execution::threading::ThreadPool> handler_worker_pool_;
    std::shared_ptr<execution::threading::ThreadPool> batch_worker_pool_;
    std::shared_ptr<execution::threading::ThreadPool> page_worker_pool_;
    std::shared_ptr<execution::threading::ThreadPool> resource_worker_pool_;
    std::shared_ptr<execution::threading::ThreadPool> parse_worker_pool_;
    std::shared_ptr<execution::threading::ThreadPool> persistence_worker_pool_;

    std::unique_ptr<music::persistence::sqlite::ConnectionPool> sqlite_pool_;
    std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> mysql_pool_;
    std::shared_ptr<http::client::HttpClient> http_client_;
    std::shared_ptr<http::client::AsioHttpClient> asio_http_client_;
    std::shared_ptr<http::client::AsioHttpClient> rclone_http_client_;
    std::shared_ptr<storage::filesystem::FileStorage> file_storage_;
    ResourceTransferAssembly resource_targets_;                                                  // 固定配置装配的独立目标集合
    std::shared_ptr<ResourceTransferRuntime> resource_runtime_;                                  // 正式逐端调度及关闭排空
    std::shared_ptr<application::use_cases::ResourceTransferService> resource_transfer_service_; // v2 受理与历史查询
    std::shared_ptr<application::ports::SongResourceCloudUploader> song_resource_cloud_uploader_;
    std::shared_ptr<application::ports::SongResourceBatchCloudUploader> song_resource_batch_cloud_uploader_;
    std::shared_ptr<application::ports::AsyncSongResourceRemoteCopier> async_song_resource_remote_copier_;
    std::shared_ptr<application::ports::MetadataParser> parser_;
    std::shared_ptr<application::ports::SongResourceParser> song_resource_parser_;
    std::shared_ptr<application::ports::IMusicProvider> music_provider_;
    std::shared_ptr<application::ports::MiguPacmtokenStore> migu_pacmtoken_store_;
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> migu_pacmtoken_refresher_;
    std::shared_ptr<application::ports::SongResourceContentFetcher> song_resource_content_fetcher_;
    std::shared_ptr<application::ports::AsyncSongResourceProvider> async_song_resource_provider_;
    std::shared_ptr<application::ports::AsyncSongResourceContentFetcher> async_song_resource_content_fetcher_;
    std::shared_ptr<execution::asio::AsioExecutionContext> async_execution_context_;
    std::shared_ptr<application::ports::AsyncRuntime> async_runtime_;
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics_;

    std::shared_ptr<application::ports::SongRepository> song_repository_;
    std::shared_ptr<application::ports::SongSourceRepository> song_source_repository_;
    std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository_;
    std::shared_ptr<application::ports::SingerSourceSyncQueue> singer_source_sync_queue_;
    std::shared_ptr<application::ports::AlbumSourceRepository> album_source_repository_;
    std::shared_ptr<application::ports::AlbumRepository> album_repository_;
    std::shared_ptr<application::ports::SingerRepository> singer_repository_;
    std::shared_ptr<application::ports::BatchTaskRepository> batch_task_repository_;
    std::shared_ptr<application::ports::MusicCatalogQueryRepository> catalog_query_repository_;
    std::shared_ptr<application::ports::SongResourceQueue> song_resource_queue_;
    std::shared_ptr<application::ports::ResourceTransferAccess> resource_access_; // 实时维护与历史跳过策略

    std::shared_ptr<application::use_cases::CrawlerService> crawler_service_;
    std::shared_ptr<application::use_cases::MiguPacmtokenService> migu_pacmtoken_service_;
    std::shared_ptr<api::controllers::CrawlerController> crawler_controller_;

    std::unique_ptr<http::server::HttpServer> http_server_;
};

ServerApplication::ServerApplication(const Config &config) : impl_(std::make_unique<Impl>(config))
{}

ServerApplication::~ServerApplication()
{
    stop();
}

bool ServerApplication::initialize()
{
    try {
        validateResourcePipelineConfiguration(impl_->config_);
        const bool parallel = impl_->config_.resource_pipeline.mode == "parallel";
        logApp(logging::LogLevel::L_INFO, "Initializing crawler components...");

        const int parse_worker_count = (std::max)(1, impl_->config_.parse_worker_threads);
        impl_->parse_worker_pool_ = std::make_shared<execution::threading::ThreadPool>(parse_worker_count);
        const int async_worker_count = (std::max)(1, impl_->config_.async_worker_threads);
        impl_->handler_worker_pool_ = std::make_shared<execution::threading::ThreadPool>(static_cast<std::size_t>(async_worker_count));
        impl_->batch_worker_pool_ = std::make_shared<execution::threading::ThreadPool>(static_cast<std::size_t>((std::max)(1, impl_->config_.max_batch_concurrency)));
        impl_->page_worker_pool_ = std::make_shared<execution::threading::ThreadPool>(static_cast<std::size_t>((std::max)(1, impl_->config_.max_page_concurrency)));
        impl_->resource_worker_pool_ = std::make_shared<execution::threading::ThreadPool>(static_cast<std::size_t>((std::max)(1, impl_->config_.resource_worker_threads)));

        impl_->asio_http_client_ = std::make_shared<http::client::AsioHttpClient>(makeHttpClientOptions(impl_->config_));
        impl_->http_client_ = impl_->asio_http_client_;
        const ResourceTransferFactory transfer_factory(impl_->config_);
        const auto legacy_provider = parallel ? std::optional<application::model::ResourceDestination> {} : transfer_factory.legacyProvider();
        std::vector<ResourceCloudTarget> legacy_clouds;
        if (impl_->config_.resource_cloud_upload_enabled && legacy_provider) legacy_clouds.push_back({*legacy_provider, std::filesystem::u8path(impl_->config_.storage_path)});
        impl_->resource_targets_ = transfer_factory.build(true, legacy_clouds);
        const auto legacy_cloud = parallel ? nullptr : legacy_provider ? impl_->resource_targets_.cloud(*legacy_provider) :
                                                                         transfer_factory.legacyCloud();
        if (!parallel && impl_->config_.resource_cloud_upload_enabled && !legacy_cloud) throw std::invalid_argument("legacy cloud configuration invalid");
        impl_->file_storage_ = impl_->resource_targets_.local;
        if (!impl_->file_storage_ || std::any_of(impl_->resource_targets_.clouds.begin(), impl_->resource_targets_.clouds.end(), [](const auto &target) { return !target.store; })) {
            logApp(logging::LogLevel::L_ERROR, "Legacy resource destination configuration is invalid.");
            return false;
        }
        impl_->parser_ = std::make_shared<music::migu::parser::MiguParser>();
        impl_->song_resource_parser_ = std::make_shared<music::migu::parser::MiguSongResourceParser>();
        impl_->migu_pacmtoken_store_ = std::make_shared<music::migu::provider::MiguRuntimePacmtokenStore>();
        impl_->migu_pacmtoken_refresher_ = std::make_shared<music::migu::provider::MiguPacmtokenClient>(impl_->http_client_);
        impl_->music_provider_ = std::make_shared<music::migu::provider::MiguProvider>(impl_->http_client_, impl_->migu_pacmtoken_store_);
        impl_->song_resource_content_fetcher_ = std::make_shared<http::client::HttpSongResourceContentFetcher>(impl_->http_client_);
        impl_->async_song_resource_provider_ = std::make_shared<music::migu::provider::MiguAsyncSongResourceProvider>(impl_->asio_http_client_, impl_->migu_pacmtoken_store_);
        impl_->async_song_resource_content_fetcher_ = std::make_shared<http::client::AsyncHttpSongResourceContentFetcher>(impl_->asio_http_client_);

        auto persistence_components = assemblePersistence(impl_->config_);
        if (!persistence_components) {
            return false;
        }
        std::shared_ptr<application::ports::TaskExecutor> persistence_executor = persistence_components->persistence_executor;
        impl_->persistence_worker_pool_ = std::move(persistence_components->persistence_worker_pool);
        impl_->sqlite_pool_ = std::move(persistence_components->sqlite_pool);
        impl_->mysql_pool_ = std::move(persistence_components->mysql_pool);
        impl_->song_repository_ = std::move(persistence_components->song_repository);
        impl_->song_source_repository_ = std::move(persistence_components->song_source_repository);
        impl_->singer_source_repository_ = std::move(persistence_components->singer_source_repository);
        impl_->singer_source_sync_queue_ = std::move(persistence_components->singer_source_sync_queue);
        impl_->album_source_repository_ = std::move(persistence_components->album_source_repository);
        impl_->album_repository_ = std::move(persistence_components->album_repository);
        impl_->singer_repository_ = std::move(persistence_components->singer_repository);
        impl_->batch_task_repository_ = std::move(persistence_components->batch_task_repository);
        impl_->catalog_query_repository_ = std::move(persistence_components->catalog_query_repository);
        impl_->song_resource_queue_ = std::move(persistence_components->song_resource_queue);
        bool resource_writes_allowed = true;
        bool local_writes_allowed = true;
        if (impl_->mysql_pool_) {
            impl_->resource_access_ = std::make_shared<music::persistence::mysql::MySqlResourceTransferAccess>(impl_->mysql_pool_);
            const auto allowed = impl_->resource_access_->writesAllowed();
            resource_writes_allowed = allowed.isSuccess() && *allowed.data();
            const auto local_ready = impl_->resource_access_->destinationReady(application::model::ResourceDestination::Local);
            local_writes_allowed = resource_writes_allowed && local_ready.isSuccess() && *local_ready.data();
            if (!resource_writes_allowed) logApp(logging::LogLevel::L_INFO, "Resource maintenance: write admission and automatic recovery are paused.");
        }
        if (auto queue = std::dynamic_pointer_cast<music::persistence::mysql::MySqlSongResourceQueue>(impl_->song_resource_queue_); !parallel && queue && local_writes_allowed) {
            auto inspector = std::make_shared<music::persistence::mysql::MySqlResourceWriteInspector>(impl_->mysql_pool_);
            const auto recovered = impl_->file_storage_->recoverOwnedResourceWrites([inspector](const std::string &owner, std::uint64_t resource, std::uint64_t generation, const std::string &path) {
                return inspector->inspect(owner, resource, generation, path);
            });
            if (recovered > 0) logApp(logging::LogLevel::L_INFO, "Recovered resource file journals: " + std::to_string(recovered));
        }
        auto blocking_storage_executor = impl_->persistence_worker_pool_ ? std::static_pointer_cast<application::ports::TaskExecutor>(impl_->persistence_worker_pool_) : std::static_pointer_cast<application::ports::TaskExecutor>(impl_->resource_worker_pool_);
        impl_->async_execution_context_ = std::make_shared<execution::asio::AsioExecutionContext>(static_cast<std::size_t>((std::max)(1, impl_->config_.outbound_http_io_threads)));
        impl_->async_runtime_ = std::make_shared<execution::asio::AsioAsyncRuntime>(impl_->async_execution_context_->executor(), blocking_storage_executor);
        if (isRcloneBackend(impl_->config_.resource_storage_backend)) {
            if (impl_->config_.rclone.rc_url.empty() || impl_->config_.rclone.target_fs.empty()) {
                logApp(logging::LogLevel::L_ERROR, "Rclone resource storage requires CRAWLER_RCLONE_RC_URL and CRAWLER_RCLONE_TARGET_FS.");
                return false;
            }
            impl_->config_.rclone.enabled = true;
            auto rclone_http_options = makeHttpClientOptions(impl_->config_);
            rclone_http_options.request_timeout = std::chrono::milliseconds((std::max)(1000, impl_->config_.rclone.api_timeout_ms));
            rclone_http_options.max_response_bytes = (std::max<std::size_t>)(1024, impl_->config_.rclone.api_max_response_bytes);
            impl_->rclone_http_client_ = std::make_shared<http::client::AsioHttpClient>(std::move(rclone_http_options));
            if (isRcloneBackend(impl_->config_.resource_storage_backend)) {
                impl_->async_song_resource_remote_copier_ = std::make_shared<storage::rclone::RcloneRemoteCopier>(impl_->rclone_http_client_, impl_->async_runtime_, impl_->config_.rclone);
            }
            logApp(logging::LogLevel::L_INFO, "Rclone resource storage enabled.");
        }
        if (impl_->config_.resource_cloud_upload_enabled) {
            logApp(logging::LogLevel::L_INFO, "Metadata cover cloud upload is enabled; song resource upload assembly remains local.");
        }

        impl_->diagnostics_ = std::make_shared<observability::logging::ConsoleDiagnosticsSink>();

        application::use_cases::CrawlerServiceDependencies crawler_dependencies;
        crawler_dependencies.resource_processing.transfer_access = impl_->resource_access_;
        if (const char *mrc_key = std::getenv("CRAWLER_MIGU_MRC_KEY"); mrc_key && *mrc_key && !isRcloneBackend(impl_->config_.resource_storage_backend)) {
            crawler_dependencies.resource_processing.post_processor = makeMiguLyricProcessingWorkflow(impl_->config_.storage_path, mrc_key, impl_->mysql_pool_);
        }
        crawler_dependencies.providers.raw_page_fetcher = impl_->music_provider_;
        crawler_dependencies.providers.song_search_provider = impl_->music_provider_;
        crawler_dependencies.providers.album_search_provider = impl_->music_provider_;
        crawler_dependencies.providers.singer_catalog_provider = impl_->music_provider_;
        crawler_dependencies.providers.album_detail_provider = impl_->music_provider_;
        crawler_dependencies.providers.song_resource_provider = impl_->music_provider_;
        crawler_dependencies.providers.song_resource_content_fetcher = impl_->song_resource_content_fetcher_;
        crawler_dependencies.providers.async_song_resource_provider = impl_->async_song_resource_provider_;
        crawler_dependencies.providers.async_song_resource_content_fetcher = impl_->async_song_resource_content_fetcher_;
        crawler_dependencies.providers.migu_pacmtoken_refresher = impl_->migu_pacmtoken_refresher_;
        crawler_dependencies.providers.migu_pacmtoken_store = impl_->migu_pacmtoken_store_;

        crawler_dependencies.parsers.parse_result_parser = impl_->parser_;
        crawler_dependencies.parsers.song_source_parser = impl_->parser_;
        crawler_dependencies.parsers.singer_source_parser = impl_->parser_;
        crawler_dependencies.parsers.album_source_parser = impl_->parser_;
        crawler_dependencies.parsers.singer_profile_parser = impl_->parser_;
        crawler_dependencies.parsers.page_inspector = impl_->parser_;
        crawler_dependencies.parsers.song_resource_parser = impl_->song_resource_parser_;

        crawler_dependencies.persistence.metadata_batch_writer = persistence_components->metadata_batch_writer;
        crawler_dependencies.persistence.file_store = impl_->file_storage_;
        crawler_dependencies.persistence.song_repository = impl_->song_repository_;
        crawler_dependencies.persistence.album_repository = impl_->album_repository_;
        crawler_dependencies.persistence.singer_repository = impl_->singer_repository_;
        crawler_dependencies.persistence.song_source_repository = impl_->song_source_repository_;
        crawler_dependencies.persistence.singer_source_repository = impl_->singer_source_repository_;
        crawler_dependencies.persistence.singer_source_sync_queue = impl_->singer_source_sync_queue_;
        crawler_dependencies.persistence.album_source_writer = impl_->album_source_repository_;
        crawler_dependencies.persistence.album_source_sync_queue = impl_->album_source_repository_;
        crawler_dependencies.persistence.batch_task_repository = impl_->batch_task_repository_;
        crawler_dependencies.persistence.catalog_query_repository = impl_->catalog_query_repository_;
        crawler_dependencies.persistence.song_resource_queue = impl_->song_resource_queue_;
        crawler_dependencies.persistence.song_resource_blob_store = impl_->file_storage_;
        crawler_dependencies.persistence.song_resource_cloud_uploader = impl_->song_resource_cloud_uploader_;
        crawler_dependencies.persistence.song_resource_batch_cloud_uploader = impl_->song_resource_batch_cloud_uploader_;
        crawler_dependencies.persistence.async_song_resource_remote_copier = impl_->async_song_resource_remote_copier_;

        crawler_dependencies.executors.parse_executor = impl_->parse_worker_pool_;
        crawler_dependencies.executors.batch_executor = impl_->batch_worker_pool_;
        crawler_dependencies.executors.page_executor = impl_->page_worker_pool_;
        crawler_dependencies.executors.persistence_executor = persistence_executor;
        crawler_dependencies.executors.resource_executor = impl_->resource_worker_pool_;
        crawler_dependencies.executors.async_runtime = impl_->async_runtime_;

        crawler_dependencies.options.max_batch_concurrency = impl_->config_.max_batch_concurrency;
        crawler_dependencies.options.max_page_concurrency = impl_->config_.max_page_concurrency;
        crawler_dependencies.options.max_retries = impl_->config_.max_retries;
        crawler_dependencies.options.task_timeout = std::chrono::milliseconds(impl_->config_.task_timeout_ms);
        crawler_dependencies.options.runtime_settings = makeRuntimeSettings(impl_->config_);
        crawler_dependencies.diagnostics = impl_->diagnostics_;

        impl_->crawler_service_ = std::make_shared<application::use_cases::CrawlerService>(std::move(crawler_dependencies));
        impl_->migu_pacmtoken_service_ = std::make_shared<application::use_cases::MiguPacmtokenService>(
            impl_->migu_pacmtoken_refresher_,
            impl_->migu_pacmtoken_store_
        );

        if (!parallel && resource_writes_allowed && local_writes_allowed) {
            try {
                application::model::MiguStaleRecoveryRequest recovery_request;
                recovery_request.stale_after_seconds = (std::max)(60, impl_->config_.task_timeout_ms / 1000);
                const auto recovered = impl_->crawler_service_->recoverMiguStale(std::move(recovery_request));
                const int recovered_total = recovered.singer_songs_recovered + recovered.singer_albums_recovered + recovered.album_details_recovered + recovered.album_songs_recovered + recovered.resources_recovered + recovered.batches_recovered;
                if (recovered_total > 0) {
                    std::ostringstream out;
                    out << "Recovered stale Migu running states: singer_songs=" << recovered.singer_songs_recovered
                        << ", singer_albums=" << recovered.singer_albums_recovered
                        << ", album_details=" << recovered.album_details_recovered
                        << ", album_songs=" << recovered.album_songs_recovered
                        << ", resources=" << recovered.resources_recovered
                        << ", batches=" << recovered.batches_recovered;
                    logApp(logging::LogLevel::L_INFO, out.str());
                }
            } catch (const std::exception &e) {
                logApp(logging::LogLevel::L_WARN, std::string("Stale Migu recovery skipped: ") + e.what());
            }
        }

        auto cover_service = makeMetadataCoverService(impl_->mysql_pool_, impl_->file_storage_, impl_->http_client_, impl_->resource_worker_pool_, impl_->diagnostics_, impl_->batch_task_repository_, std::chrono::milliseconds(impl_->config_.task_timeout_ms), legacy_cloud, impl_->config_.resource_cloud_timeout_ms, impl_->resource_access_, legacy_provider);
        impl_->crawler_controller_ = std::make_shared<api::controllers::CrawlerController>(
            impl_->crawler_service_,
            makeReadonlySettingsSnapshot(impl_->config_),
            impl_->migu_pacmtoken_service_,
            cover_service
        );

        impl_->http_server_ = std::make_unique<http::server::DrogonHttpServer>(impl_->config_.http_worker_threads, impl_->handler_worker_pool_);
        impl_->http_server_->bind(impl_->config_.host, impl_->config_.port);

        if (impl_->mysql_pool_) {
            auto jobs = std::make_shared<music::persistence::mysql::MySqlResourceTransferJobs>(impl_->mysql_pool_);
            impl_->resource_runtime_ = makeResourceTransferRuntime(impl_->config_, impl_->mysql_pool_, jobs, impl_->migu_pacmtoken_store_);
            auto queries = std::make_shared<music::persistence::mysql::MySqlResourceTransferQueries>(impl_->mysql_pool_);
            auto producers = std::make_shared<music::persistence::mysql::MySqlResourceProducerTasks>(impl_->mysql_pool_);
            impl_->resource_transfer_service_ = std::make_shared<application::use_cases::ResourceTransferService>(jobs, impl_->resource_access_, queries, impl_->resource_runtime_, producers);
            auto compatibility_repository = std::make_shared<music::persistence::mysql::MySqlLegacyResourceTransfer>(impl_->mysql_pool_);
            impl_->resource_transfer_service_->setLegacyRepository(compatibility_repository, parallel && !impl_->config_.resource_pipeline.legacy_view_provider.empty());
            if (parallel && !impl_->config_.resource_pipeline.legacy_view_provider.empty()) {
                const auto fixed_cloud = application::model::parseCloudProvider(impl_->config_.resource_pipeline.legacy_view_provider);
                auto reader = std::make_shared<storage::filesystem::MetadataCoverStore>(impl_->http_client_, impl_->file_storage_, std::vector<std::string> {});
                auto compatibility = std::make_shared<application::use_cases::LegacyResourceTransferService>(compatibility_repository, impl_->resource_runtime_, impl_->resource_access_, fixed_cloud, true, reader, impl_->config_.task_timeout_ms);
                impl_->crawler_controller_->setLegacyResourceTransfers(compatibility);
                if (cover_service) cover_service->setProjection(compatibility);
                // Controller 持有 service；观察回调只在一次调用期间延长生命，避免 service/runtime 环。
                impl_->resource_runtime_->addObserver([weak = std::weak_ptr<application::use_cases::LegacyResourceTransferService>(compatibility)] {
                    const auto service = weak.lock();
                    return service && service->pollNext();
                });
            }
        }
        api::routing::CrawlerRoutes crawler_routes(impl_->crawler_controller_);
        if (parallel) {
            api::routing::ResourceParallelLegacyRouteSink guarded(*impl_->http_server_, !impl_->config_.resource_pipeline.legacy_view_provider.empty(), impl_->resource_access_);
            crawler_routes.registerRoutes(guarded);
        } else if (impl_->resource_access_) {
            api::routing::ResourceWriteRouteSink guarded(*impl_->http_server_, impl_->resource_access_);
            crawler_routes.registerRoutes(guarded);
        } else {
            crawler_routes.registerRoutes(*impl_->http_server_);
        }
        api::routing::ResourceTransferRoutes(impl_->resource_transfer_service_).registerRoutes(*impl_->http_server_);

        logApp(logging::LogLevel::L_INFO, "Crawler initialization complete.");
        return true;
    } catch (const std::exception &e) {
        logApp(logging::LogLevel::L_ERROR, std::string("Initialization failed: ") + e.what());
        return false;
    }
}

void ServerApplication::run()
{
    if (impl_->resource_runtime_) impl_->resource_runtime_->start();
    if (impl_->http_server_) {
        impl_->http_server_->run();
    }
}

void ServerApplication::stop()
{
    if (impl_->http_server_) {
        impl_->http_server_->stop();
    }
    if (impl_->resource_runtime_ && !impl_->resource_runtime_->shutdown()) logApp(logging::LogLevel::L_WARN, "Resource shutdown grace expired; pending attempts require recovery.");
    if (impl_->mysql_pool_) {
        impl_->mysql_pool_->cancel();
    }
}

// 这个函数既支持 URL 直接爬取，也支持关键词搜索爬取
bool ServerApplication::executeCrawl(const std::string &target)
{
    if (impl_->config_.resource_pipeline.mode == "parallel") {
        logApp(logging::LogLevel::L_WARN, "CLI legacy writes are unavailable in parallel resource mode.");
        return false;
    }
    if (!impl_->crawler_service_) {
        logApp(logging::LogLevel::L_ERROR, "CrawlerService not initialized.");
        return false;
    }

    if (impl_->resource_access_) {
        const auto allowed = impl_->resource_access_->writesAllowed();
        if (!allowed.isSuccess() || !*allowed.data()) {
            logApp(logging::LogLevel::L_WARN, "Resource maintenance: crawl command was not admitted.");
            return false;
        }
    }

    logApp(logging::LogLevel::L_INFO, "CLI Mode: Crawling " + target + "...");
    if (isUrl(target)) {
        return impl_->crawler_service_->processUrlSync(target, 20);
    }
    return impl_->crawler_service_->searchAndSaveSync(target, 20);
}

} // namespace orchestrator::modules::crawler::infrastructure::app
