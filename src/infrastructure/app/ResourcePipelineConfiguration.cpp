#include "crawler/infrastructure/app/ResourcePipelineConfiguration.h"

#include "crawler/infrastructure/platform/Environment.h"

#include <charconv>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::app {
namespace {
using Environment = platform::Environment;
// 数值格式错误不得使已配置的限速或边界悄悄失效。
std::uint64_t number(const std::string &key, std::uint64_t fallback, std::uint64_t maximum)
{
    const auto value = Environment::getString(key);
    if (!value) return fallback;
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (result.ec != std::errc() || result.ptr != value->data() + value->size() || parsed > maximum) throw std::invalid_argument("invalid resource pipeline numeric configuration");
    return parsed;
}
// 布尔开关只接受明确值，避免误启用上传。
bool enabled(const std::string &key, bool fallback)
{
    const auto value = Environment::getString(key);
    if (!value) return fallback;
    if (*value == "true" || *value == "1") return true;
    if (*value == "false" || *value == "0") return false;
    throw std::invalid_argument("invalid resource pipeline boolean configuration");
}
// 按端复制 RC 值，绝不修改另一端或旧全局配置。
storage::rclone::RcloneConfig rclone(const storage::rclone::RcloneConfig &base, const std::string &prefix)
{
    auto value = base;
    value.rc_url = Environment::getStringOr(prefix + "_RCLONE_RC_URL", value.rc_url);
    value.rc_user = Environment::getStringOr(prefix + "_RCLONE_RC_USER", value.rc_user);
    value.rc_password = Environment::getStringOr(prefix + "_RCLONE_RC_PASSWORD", value.rc_password);
    value.target_fs = Environment::getStringOr(prefix + "_RCLONE_TARGET_FS", value.target_fs);
    return value;
}
} // namespace

void readResourcePipelineEnvironment(ServerApplication::Config &config)
{
    auto &pipeline = config.resource_pipeline;
    pipeline.mode = Environment::getStringOr("CRAWLER_RESOURCE_PIPELINE_MODE", pipeline.mode);
    if (pipeline.mode == "legacy") return;
    pipeline.targets[0].enabled = enabled("CRAWLER_RESOURCE_LOCAL_ENABLED", true);
    pipeline.targets[0].adapter = "filesystem";
    pipeline.targets[1].adapter = Environment::getStringOr("CRAWLER_RESOURCE_BAIDU_ADAPTER", "baidupcs");
    pipeline.targets[2].adapter = Environment::getStringOr("CRAWLER_RESOURCE_GDRIVE_ADAPTER", "rclone");
    const auto providers = Environment::getStringOr("CRAWLER_RESOURCE_CLOUD_PROVIDERS", "");
    std::set<std::string> selected;
    for (std::size_t start = 0; start < providers.size();) {
        const auto end = providers.find(',', start);
        const auto name = providers.substr(start, end == std::string::npos ? providers.size() - start : end - start);
        const auto destination = application::model::parseCloudProvider(name);
        if (!destination || !selected.insert(name).second) throw std::invalid_argument("invalid resource cloud provider list");
        pipeline.targets[static_cast<std::size_t>(*destination)].enabled = true;
        if (end == std::string::npos) break;
        start = end + 1;
        if (start == providers.size()) throw std::invalid_argument("invalid resource cloud provider list");
    }
    const std::array<std::string, 3> names {"LOCAL", "BAIDU", "GDRIVE"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        auto &target = pipeline.targets[index];
        const auto prefix = "CRAWLER_RESOURCE_" + names[index];
        target.concurrency = static_cast<int>(number(prefix + "_CONCURRENCY", 3, 32));
        target.policy.timeout_ms = static_cast<int>(number(prefix + "_TIMEOUT_MS", 120000, 600000));
        target.policy.retries = static_cast<int>(number(prefix + "_RETRIES", 1, 5));
        target.policy.max_bytes = number(prefix + "_MAX_BYTES", target.policy.max_bytes, 4ULL * 1024 * 1024 * 1024);
        target.policy.bytes_per_second = number(prefix + "_MAX_BYTES_PER_SECOND", 0, 1ULL << 40);
        const auto default_staging = (std::filesystem::u8path(config.storage_path) / ".resource-transfer-staging/local").generic_u8string();
        target.staging_path = Environment::getStringOr(prefix + "_STAGING_PATH", index == 0 ? std::string(reinterpret_cast<const char *>(default_staging.data()), default_staging.size()) : "");
        target.staging_max_bytes = number(prefix + "_STAGING_MAX_BYTES", target.staging_max_bytes, 1ULL << 40);
        if (index) target.rclone = rclone(config.rclone, prefix);
    }
    pipeline.source_concurrency = static_cast<int>(number("CRAWLER_RESOURCE_SOURCE_CONCURRENCY", 6, 32));
    pipeline.transfer_concurrency = static_cast<int>(number("CRAWLER_RESOURCE_TRANSFER_CONCURRENCY", 9, 96));
    pipeline.shutdown_grace_ms = static_cast<int>(number("CRAWLER_RESOURCE_SHUTDOWN_GRACE_MS", 30000, 600000));
    pipeline.verification_policy = Environment::getStringOr("CRAWLER_RESOURCE_CLOUD_VERIFICATION_POLICY", "hash_match");
    pipeline.producer_enabled = enabled("CRAWLER_RESOURCE_PRODUCER_ENABLED", true);
    pipeline.producer_concurrency = static_cast<int>(number("CRAWLER_RESOURCE_PRODUCER_CONCURRENCY", 1, 4));
    pipeline.producer_policy.timeout_ms = static_cast<int>(number("CRAWLER_RESOURCE_PRODUCER_TIMEOUT_MS", 120000, 600000));
    pipeline.producer_policy.retries = static_cast<int>(number("CRAWLER_RESOURCE_PRODUCER_RETRIES", 2, 5));
    pipeline.legacy_view_provider = Environment::getStringOr("CRAWLER_RESOURCE_LEGACY_VIEW_PROVIDER", "");
    validateResourcePipelineConfiguration(config);
}

void validateResourcePipelineConfiguration(const ServerApplication::Config &config)
{
    const auto &pipeline = config.resource_pipeline;
    if (pipeline.mode == "legacy") return;
    if (pipeline.mode != "parallel" || config.persistence_backend != "mysql" || config.resource_storage_backend != "filesystem") throw std::invalid_argument("invalid parallel resource mode");
    int count = 0;
    for (const auto &target : pipeline.targets) {
        if (!target.enabled) continue;
        ++count;
        if (target.concurrency < 1 || target.concurrency > 32 || target.policy.timeout_ms < 100 || target.policy.timeout_ms > 600000 || target.policy.retries < 0 || target.policy.retries > 5 || target.policy.max_bytes == 0 || target.policy.max_bytes > 4ULL * 1024 * 1024 * 1024) throw std::invalid_argument("invalid resource pipeline budget");
    }
    if (count == 0 || pipeline.source_concurrency < count || pipeline.source_concurrency > 32 || pipeline.transfer_concurrency < count || pipeline.transfer_concurrency > 96 || pipeline.shutdown_grace_ms < 0 || pipeline.shutdown_grace_ms > 600000) throw std::invalid_argument("insufficient resource pipeline capacity");
    if (pipeline.verification_policy != "hash_match" && pipeline.verification_policy != "strict_sha256") throw std::invalid_argument("invalid cloud verification policy");
    if (pipeline.producer_concurrency < 1 || pipeline.producer_concurrency > 4 || pipeline.producer_policy.timeout_ms < 100 || pipeline.producer_policy.timeout_ms > 600000 || pipeline.producer_policy.retries < 0 || pipeline.producer_policy.retries > 5 || pipeline.producer_policy.max_bytes == 0 || pipeline.producer_policy.max_bytes > 1024 * 1024) throw std::invalid_argument("invalid producer budget");
    if (!pipeline.legacy_view_provider.empty() && pipeline.legacy_view_provider != "local" && !application::model::parseCloudProvider(pipeline.legacy_view_provider)) throw std::invalid_argument("invalid legacy resource view");
}

} // namespace orchestrator::modules::crawler::infrastructure::app
