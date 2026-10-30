#include "crawler/infrastructure/app/CloudFileStoreComposition.h"

#include "crawler/infrastructure/storage/baidupcs/BaiduCloudFileStore.h"
#include "crawler/infrastructure/storage/rclone/RcloneCloudFileStore.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {

std::shared_ptr<application::ports::CloudFileStore> makeCloudFileStore(const ServerApplication::Config &config, std::shared_ptr<http::client::HttpClient> rclone_client, execution::process::CommandRunner runner)
{
    if (!config.resource_cloud_upload_enabled) return nullptr;
    auto backend = config.resource_cloud_upload_backend;
    std::transform(backend.begin(), backend.end(), backend.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (backend == "baidupcs") {
        auto baidu = config.baidupcs;
        baidu.enabled = true;
        baidu.local_root = std::filesystem::u8path(config.storage_path);
        return std::make_shared<storage::baidupcs::BaiduCloudFileStore>(std::move(baidu), std::move(runner));
    }
    if (backend == "rclone") return std::make_shared<storage::rclone::RcloneCloudFileStore>(std::move(rclone_client), config.rclone);
    throw std::invalid_argument("unsupported metadata cover cloud backend");
}

} // namespace orchestrator::modules::crawler::infrastructure::app
