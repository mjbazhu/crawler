#include "crawler/infrastructure/app/MetadataCoverComposition.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"
#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {

std::shared_ptr<application::use_cases::MetadataCoverService> makeMetadataCoverService(
    std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool,
    std::shared_ptr<storage::filesystem::FileStorage> files,
    std::shared_ptr<http::client::HttpClient> client,
    std::shared_ptr<application::ports::TaskExecutor> executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics,
    std::shared_ptr<application::ports::BatchTaskRepository> batches,
    std::chrono::milliseconds timeout,
    std::shared_ptr<application::ports::CloudFileStore> cloud,
    int cloud_timeout_ms
)
{
    if (!pool) return nullptr;
    auto queue = std::make_shared<music::persistence::mysql::MySqlMetadataCoverQueue>(std::move(pool));
    auto store = std::make_shared<storage::filesystem::MetadataCoverStore>(std::move(client), std::move(files), std::vector<std::string> {"User-Agent: Crawler/1.0", "Accept: image/*"});
    auto workflow = std::make_shared<application::use_cases::MetadataCoverDownloadWorkflow>(queue, store, store);
    auto cloud_workflow = cloud ? std::make_shared<application::use_cases::MetadataCoverCloudWorkflow>(queue, store, std::move(cloud), cloud_timeout_ms) : nullptr;
    return std::make_shared<application::use_cases::MetadataCoverService>(queue, store, std::move(workflow), std::move(executor), std::move(diagnostics), std::move(batches), timeout, std::move(cloud_workflow));
}

} // namespace orchestrator::modules::crawler::infrastructure::app
