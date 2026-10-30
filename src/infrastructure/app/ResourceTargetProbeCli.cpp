#include "ResourceTargetProbeCli.h"

#include "crawler/infrastructure/app/ResourcePipelineConfiguration.h"
#include "crawler/infrastructure/app/ResourceTargetVerifier.h"
#include "crawler/infrastructure/platform/Environment.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace orchestrator::modules::crawler::infrastructure::app {
using Environment = platform::Environment;
// 与服务相同的既有存储/RC选项；认证只在进程内使用，禁止输出原始配置。
ServerApplication::Config resourceArchiveConfiguration()
{
    const auto file = Environment::getString("CRAWLER_ENV_FILE");
    Environment::loadDotEnvFile(file ? *file : ".env");
    ServerApplication::Config config;
    config.storage_path = Environment::getStringOr("CRAWLER_STORAGE_PATH", config.storage_path);
    config.baidupcs.local_root = std::filesystem::u8path(config.storage_path);
    config.baidupcs.cloud_root = Environment::getStringOr("CRAWLER_RESOURCE_CLOUD_ROOT", config.baidupcs.cloud_root);
    config.baidupcs.executable_path = Environment::getStringOr("CRAWLER_BAIDUPCS_GO_PATH", config.baidupcs.executable_path);
    config.rclone.rc_url = Environment::getStringOr("CRAWLER_RCLONE_RC_URL", config.rclone.rc_url);
    config.rclone.rc_user = Environment::getStringOr("CRAWLER_RCLONE_RC_USER", config.rclone.rc_user);
    config.rclone.rc_password = Environment::getStringOr("CRAWLER_RCLONE_RC_PASSWORD", config.rclone.rc_password);
    config.rclone.target_fs = Environment::getStringOr("CRAWLER_RCLONE_TARGET_FS", config.rclone.target_fs);
    config.rclone.api_timeout_ms = Environment::getIntOr("CRAWLER_RCLONE_API_TIMEOUT_MS", config.rclone.api_timeout_ms);
    config.rclone.api_max_response_bytes = static_cast<std::size_t>(Environment::getIntOr("CRAWLER_RCLONE_API_MAX_RESPONSE_BYTES", static_cast<int>(config.rclone.api_max_response_bytes)));
    config.outbound_http_verify_tls = Environment::getBoolOr("CRAWLER_OUTBOUND_VERIFY_TLS", config.outbound_http_verify_tls);
    readResourcePipelineEnvironment(config);
    if (config.resource_pipeline.mode != "parallel") throw std::invalid_argument("target probe requires explicit parallel configuration");
    return config;
}

int resourceTargetProbeCommand(const std::map<std::string, std::string> &options)
{
    if (options.size() != 1 || !options.contains("--destination")) throw std::invalid_argument("target-probe requires only --destination");
    const auto destination = application::model::parseResourceDestination(options.at("--destination"));
    if (!destination) throw std::invalid_argument("destination is invalid");
    const auto config = resourceArchiveConfiguration();
    const auto &target = config.resource_pipeline.targets.at(static_cast<std::size_t>(*destination));
    if (!target.enabled) throw std::invalid_argument("probe destination is not enabled");
    application::model::ResourceTransferContext context {std::chrono::steady_clock::now() + std::chrono::seconds(10), {}};
    const auto observed = *destination == application::model::ResourceDestination::Local ? verifyLocalResourceTarget(config.storage_path, context) : verifyCloudResourceTarget(config, {*destination, target.staging_path, target.staging_max_bytes, target.adapter, target.rclone, {}}, context);
    std::cout << nlohmann::json {{"destination", application::model::resourceDestinationName(*destination)}, {"available", observed.profile.available}, {"fingerprint", observed.profile.fingerprint}, {"identity_scope", observed.identity_scope}, {"result_code", observed.result_code}, {"uploads", 0}, {"source_content_requests", 0}, {"database_connected", false}}.dump() << '\n';
    return observed.profile.available ? 0 : 2;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
