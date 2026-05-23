#include "crawler/infrastructure/app/RuntimeSettingsSnapshotFactory.h"

namespace orchestrator::modules::crawler::infrastructure::app {

application::model::CrawlerRuntimeSettings makeRuntimeSettings(const ServerApplication::Config &config)
{
    application::model::CrawlerRuntimeSettings settings;
    settings.max_batch_concurrency = config.max_batch_concurrency;
    settings.max_page_concurrency = config.max_page_concurrency;
    settings.max_retries = config.max_retries;
    settings.task_timeout_ms = config.task_timeout_ms;
    return settings;
}

api::dto::CrawlerSettingsReadonlySnapshot makeReadonlySettingsSnapshot(const ServerApplication::Config &config)
{
    api::dto::CrawlerSettingsReadonlySnapshot snapshot;
    snapshot.http_worker_threads = config.http_worker_threads;
    snapshot.async_worker_threads = config.async_worker_threads;
    snapshot.resource_worker_threads = config.resource_worker_threads;
    snapshot.parse_worker_threads = config.parse_worker_threads;
    snapshot.persistence_worker_threads = config.persistence_worker_threads;
    snapshot.outbound_http_io_threads = config.outbound_http_io_threads;
    snapshot.mysql_io_threads = config.mysql.io_threads;
    snapshot.mysql_max_pool_size = config.mysql.max_pool_size;
    snapshot.persistence_backend = config.persistence_backend;
    return snapshot;
}

} // namespace orchestrator::modules::crawler::infrastructure::app
