#include "crawler/infrastructure/app/ResourceTransferFactory.h"

#include "crawler/infrastructure/app/CloudFileStoreComposition.h"
#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/storage/filesystem/LocalResourceTransfer.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduResourceTransfer.h"
#include "crawler/infrastructure/storage/rclone/RcloneResourceTransfer.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {
namespace model = application::model;
namespace {
// 复用既有配置的大小写规则，不把后端名称当作平台身份。
std::string adapterName(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
// 每个未显式注入客户端的目标都有自己的执行器，慢端不占用另一端的 I/O 线程。
std::shared_ptr<http::client::HttpClient> rcloneClient(const ServerApplication::Config &config, std::shared_ptr<http::client::HttpClient> client)
{
    if (client) return client;
    auto options = makeHttpClientOptions(config);
    options.request_timeout = std::chrono::milliseconds((std::max)(1000, config.rclone.api_timeout_ms));
    options.max_response_bytes = (std::max<std::size_t>)(1024, config.rclone.api_max_response_bytes);
    return std::make_shared<http::client::AsioHttpClient>(std::move(options));
}
// 同一份连接及 remote 配置不能同时声明成百度和 Google；只比较，不输出认证。
bool sameRcloneTarget(const ServerApplication::Config &config, const ResourceCloudTarget &left, const ResourceCloudTarget &right)
{
    if (adapterName(left.adapter.empty() ? config.resource_cloud_upload_backend : left.adapter) != "rclone" || adapterName(right.adapter.empty() ? config.resource_cloud_upload_backend : right.adapter) != "rclone") return false;
    try {
        const auto a = storage::rclone::detail::normalizeCloudConfig(left.rclone.value_or(config.rclone));
        const auto b = storage::rclone::detail::normalizeCloudConfig(right.rclone.value_or(config.rclone));
        return a.rc_url == b.rc_url && a.rc_user == b.rc_user && a.rc_password == b.rc_password && a.target_fs == b.target_fs;
    } catch (const std::exception &) {
        // 无效配置留给该端的常规装配错误，不能影响另一端。
        return false;
    }
}
} // namespace

std::shared_ptr<application::ports::CloudFileStore> ResourceTransferAssembly::cloud(model::ResourceDestination provider) const
{
    for (const auto &target : clouds)
        if (target.provider == provider) return target.store;
    return nullptr;
}

ResourceTransferFactory::ResourceTransferFactory(ServerApplication::Config config) : config_(std::move(config))
{}

std::optional<model::ResourceDestination> ResourceTransferFactory::legacyProvider() const
{
    if (!config_.resource_cloud_upload_enabled) return std::nullopt;
    const auto adapter = adapterName(config_.resource_cloud_upload_backend);
    if (adapter != "baidupcs" && adapter != "rclone") throw std::invalid_argument("unsupported legacy cloud adapter");
    if (config_.resource_cloud_provider.empty()) return adapter == "baidupcs" ? std::optional(model::ResourceDestination::Baidu) : std::nullopt;
    const auto provider = model::parseCloudProvider(adapterName(config_.resource_cloud_provider));
    if (!provider || !model::supportsResourceTransferAdapter(*provider, adapter)) throw std::invalid_argument("invalid legacy cloud provider");
    return provider;
}

std::shared_ptr<application::ports::CloudFileStore> ResourceTransferFactory::legacyCloud(std::shared_ptr<http::client::HttpClient> client, execution::process::CommandRunner runner) const
{
    if (!config_.resource_cloud_upload_enabled) return nullptr;
    if (adapterName(config_.resource_cloud_upload_backend) == "rclone") client = rcloneClient(config_, std::move(client));
    return makeCloudFileStore(config_, std::move(client), std::move(runner));
}

std::shared_ptr<application::ports::ResourceTransferDestination> ResourceTransferAssembly::destination(model::ResourceDestination provider) const
{
    if (provider == model::ResourceDestination::Local) return local_transfer;
    for (const auto &target : clouds)
        if (target.provider == provider) return target.transfer;
    return nullptr;
}

ResourceTransferAssembly ResourceTransferFactory::build(bool local_enabled, const std::vector<ResourceCloudTarget> &clouds, std::shared_ptr<http::client::HttpClient> rclone_client, execution::process::CommandRunner runner) const
{
    std::set<model::ResourceDestination> selected;
    for (const auto &target : clouds)
        if ((target.provider != model::ResourceDestination::Baidu && target.provider != model::ResourceDestination::Gdrive) || !selected.insert(target.provider).second)
            throw std::invalid_argument("invalid or duplicated cloud provider");
    ResourceTransferAssembly result;
    const bool ambiguous = clouds.size() == 2 && sameRcloneTarget(config_, clouds[0], clouds[1]);
    if (local_enabled) {
        try {
            if (config_.storage_path.empty()) throw std::invalid_argument("local root is empty");
            result.local = std::make_shared<storage::filesystem::FileStorage>(std::filesystem::u8path(config_.storage_path), false);
            result.local_transfer = std::make_shared<storage::filesystem::LocalResourceTransfer>(result.local);
        } catch (const std::exception &) {
            result.local_result_code = "local_configuration_invalid";
        }
    }
    for (const auto &target : clouds) {
        ResourceCloudAssembly assembly {target.provider, nullptr, {}};
        if (ambiguous) {
            assembly.result_code = assembly.transfer_result_code = "cloud_target_ambiguous";
            result.clouds.push_back(std::move(assembly));
            continue;
        }
        try {
            auto config = config_;
            config.resource_cloud_upload_enabled = true;
            config.resource_cloud_upload_backend = adapterName(target.adapter.empty() ? config.resource_cloud_upload_backend : target.adapter);
            assembly.adapter = config.resource_cloud_upload_backend;
            if (!model::supportsResourceTransferAdapter(target.provider, config.resource_cloud_upload_backend)) throw std::invalid_argument("cloud adapter does not support provider");
            if (target.rclone) config.rclone = *target.rclone;
            const auto staging = target.staging_root.generic_u8string();
            config.storage_path.assign(reinterpret_cast<const char *>(staging.data()), staging.size());
            const bool native_baidu = config.resource_cloud_upload_backend == "baidupcs";
            if (native_baidu && target.staging_root.empty()) throw std::invalid_argument("BaiduPCS staging root is required");
            auto client = target.rclone_client ? target.rclone_client : rclone_client;
            if (!native_baidu) client = rcloneClient(config, std::move(client));
            assembly.store = makeCloudFileStore(config, client, runner);
            try {
                if (native_baidu) {
                    auto baidu = config.baidupcs;
                    baidu.local_root = target.staging_root;
                    assembly.transfer = std::make_shared<storage::baidupcs::BaiduResourceTransfer>(std::move(baidu), runner, target.temporary_bytes);
                } else
                    assembly.transfer = std::make_shared<storage::rclone::RcloneResourceTransfer>(client, config.rclone, target.provider);
            } catch (const std::exception &) {
                assembly.transfer_result_code = "cloud_transfer_configuration_invalid";
            }
        } catch (const std::exception &) {
            assembly.result_code = "cloud_configuration_invalid";
            assembly.transfer_result_code = "cloud_transfer_configuration_invalid";
        }
        result.clouds.push_back(std::move(assembly));
    }
    return result;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
