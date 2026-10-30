#include "crawler/infrastructure/app/ResourceTransferComposition.h"

#include "ResourceTransferIdentityFence.h"
#include "crawler/infrastructure/app/ResourcePipelineConfiguration.h"
#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/ResourceContextHttpClient.h"
#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/music/migu/provider/MiguPacmtokenClient.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferSources.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"
#include "crawler/infrastructure/music/migu/lyrics/MiguMrcLyricsDecoder.h"
#include "crawler/infrastructure/music/lyrics/TimedLyricsEncoder.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/storage/filesystem/ResourceSnapshotSource.h"
#include "crawler/application/use_cases/ResourceTransferWorker.h"
#include "crawler/application/use_cases/ResourceProducerWorker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {
namespace model = application::model;
namespace fs = storage::filesystem;
namespace {
// 每端共享自己的 HTTP 执行器，每项重新绑定截止/取消，不重建来源空间配额。
std::shared_ptr<fs::ResourceSnapshotSource> source(const ServerApplication::Config &config, const ResourcePipelineTarget &target, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool, std::shared_ptr<application::ports::MiguPacmtokenStore> tokens, std::shared_ptr<fs::ResourceArtifactProducer> producer)
{
    auto options = makeHttpClientOptions(config);
    options.io_threads = 1;
    auto client = std::make_shared<http::client::AsioHttpClient>(options);
    fs::ResourceSnapshotDependencies base;
    base.descriptions = std::make_shared<music::persistence::mysql::MySqlResourceTransferSources>(std::move(pool));
    base.parser = std::make_shared<music::migu::parser::MiguSongResourceParser>();
    base.token_store = std::move(tokens);
    base.artifacts = std::make_shared<fs::FileStorage>(std::filesystem::u8path(config.storage_path), false);
    base.producer = std::move(producer);
    auto dependencies = base;
    dependencies.bind_context = [base, client](const model::ResourceTransferContext &context) {
        auto value = base;
        auto bounded = std::make_shared<http::client::ResourceContextHttpClient>(client, context);
        value.execution_context = bounded->executionContext();
        value.songs = std::make_shared<music::migu::provider::MiguProvider>(bounded, base.token_store);
        value.content = std::make_shared<http::client::HttpSongResourceContentFetcher>(bounded);
        value.token_refresher = std::make_shared<music::migu::provider::MiguPacmtokenClient>(bounded);
        value.covers = bounded;
        return value;
    };
    std::array<std::filesystem::path, 3> roots;
    roots.fill(std::filesystem::u8path(target.staging_path));
    return std::make_shared<fs::ResourceSnapshotSource>(std::move(dependencies), std::move(roots), target.staging_max_bytes);
}
} // namespace

std::shared_ptr<ResourceTransferRuntime> makeResourceTransferRuntime(const ServerApplication::Config &config, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool, std::shared_ptr<application::ports::ResourceTransferJobRepository> jobs, std::shared_ptr<application::ports::MiguPacmtokenStore> tokens)
{
    validateResourcePipelineConfiguration(config);
    const auto &pipeline = config.resource_pipeline;
    if (pipeline.mode != "parallel") return std::make_shared<ResourceTransferRuntime>(false, std::vector<model::ResourceTransferTargetProfile> {}, std::vector<ResourceRuntimeLane> {}, pipeline.shutdown_grace_ms);
    if (!pool || !jobs || !tokens) throw std::invalid_argument("parallel resource dependencies required");
    const auto key = platform::Environment::getStringOr("CRAWLER_MIGU_MRC_KEY", "");
    const bool key_available = !key.empty() && key.size() <= 256 && std::all_of(key.begin(), key.end(), [](unsigned char value) { return value > 0 && value < 128; });
    auto decoder = std::make_shared<music::migu::lyrics::MiguMrcLyricsDecoder>(key);
    auto encoder = std::make_shared<music::lyrics::TimedLyricsEncoder>();
    const auto producer_version = std::string(decoder->version()) + "-" + std::string(encoder->version());
    auto producer = std::make_shared<fs::ResourceArtifactProducer>(decoder, encoder, key_available);
    auto producer_tasks = std::make_shared<music::persistence::mysql::MySqlResourceProducerTasks>(pool);
    std::vector<ResourceCloudTarget> clouds;
    for (std::size_t index = 1; index < pipeline.targets.size(); ++index) {
        const auto &target = pipeline.targets[index];
        if (target.enabled) clouds.push_back({static_cast<model::ResourceDestination>(index), std::filesystem::u8path(target.staging_path), target.staging_max_bytes, target.adapter, target.rclone, {}});
    }
    const auto assembled = ResourceTransferFactory(config).build(pipeline.targets[0].enabled, clouds);
    std::array<bool, 3> reserved {};
    for (std::size_t index = 0; index < reserved.size(); ++index) reserved[index] = pipeline.targets[index].enabled;
    auto source_budget = std::make_shared<application::use_cases::ResourceTransferBudget>(pipeline.source_concurrency, reserved);
    auto transfer_budget = std::make_shared<application::use_cases::ResourceTransferBudget>(pipeline.transfer_concurrency, reserved);
    std::vector<model::ResourceTransferTargetProfile> profiles;
    std::vector<ResourceRuntimeLane> lanes;
    for (std::size_t index = 0; index < pipeline.targets.size(); ++index) {
        const auto &target = pipeline.targets[index];
        if (!target.enabled) continue;
        const auto destination = static_cast<model::ResourceDestination>(index);
        ResourceTargetProbe probe;
        if (index == 0)
            probe = [root = std::filesystem::u8path(config.storage_path)](const auto &context) { return verifyLocalResourceTarget(root, context); };
        else {
            const auto found = std::find_if(clouds.begin(), clouds.end(), [&](const auto &cloud) { return cloud.provider == destination; });
            auto client = std::make_shared<http::client::AsioHttpClient>(makeHttpClientOptions(config));
            probe = [config, cloud = *found, client](const auto &context) { return verifyCloudResourceTarget(config, cloud, context, client); };
        }
        ResourceTargetVerification checked;
        checked.profile.available = false;
        try {
            checked = probe({std::chrono::steady_clock::now() + std::chrono::milliseconds((std::min)(10000, target.policy.timeout_ms)), {}});
        } catch (const std::exception &) {
            checked.result_code = "target_identity_unavailable";
        }
        auto profile = checked.profile;
        profile.destination = destination;
        profile.adapter = target.adapter;
        profile.policy = target.policy;
        profile.policy.strict_sha256 = pipeline.verification_policy == "strict_sha256";
        profile.produces_artifacts = pipeline.producer_enabled;
        profile.producer_version = producer_version;
        profile.producer_schema_version = encoder->schemaVersion();
        profile.producer_policy = pipeline.producer_policy;
        profile.producer_key_available = key_available;
        auto transfer = assembled.destination(destination);
        profile.available = profile.available && transfer && !target.staging_path.empty() && target.staging_max_bytes >= target.policy.max_bytes && !(target.adapter == "baidupcs" && target.policy.bytes_per_second);
        profiles.push_back(profile);
        if (!profile.available) continue;
        auto snapshots = fenceResourceSource(source(config, target, pool, tokens, producer), probe);
        auto worker = std::make_shared<application::use_cases::ResourceTransferWorker>(jobs, snapshots, fenceResourceDestination(std::move(transfer), probe), profile, source_budget, transfer_budget);
        lanes.push_back({destination, target.concurrency, [worker](std::function<bool()> stopped) { const auto value = worker->runOne(std::move(stopped)); return value.isSuccess() && *value.data(); }});
        if (pipeline.producer_enabled) {
            auto background = std::make_shared<application::use_cases::ResourceProducerWorker>(producer_tasks, snapshots, producer, profile, source_budget);
            lanes.push_back({destination, pipeline.producer_concurrency, [background](std::function<bool()> stopped) { const auto value = background->runOne(std::move(stopped)); return value.isSuccess() && *value.data(); }, "producer"});
        }
    }
    for (auto &left : profiles)
        for (auto &right : profiles)
            if (left.destination != right.destination && left.destination != model::ResourceDestination::Local && right.destination != model::ResourceDestination::Local && !left.fingerprint.empty() && left.fingerprint == right.fingerprint) left.available = right.available = false;
    lanes.erase(std::remove_if(lanes.begin(), lanes.end(), [&](const auto &lane) {
                    const auto profile = std::find_if(profiles.begin(), profiles.end(), [&](const auto &value) { return value.destination == lane.destination; });
                    return profile == profiles.end() || !profile->available;
                }),
                lanes.end());
    return std::make_shared<ResourceTransferRuntime>(true, std::move(profiles), std::move(lanes), pipeline.shutdown_grace_ms);
}

} // namespace orchestrator::modules::crawler::infrastructure::app
