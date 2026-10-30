#include "ResourceArchiveCliSupport.h"
#include "ResourceTargetProbeCli.h"
#include "crawler/infrastructure/app/ResourceTargetVerifier.h"
#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/storage/rclone/RcloneDiscoveryProbe.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduDiscoveryProbe.h"
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::app::archive_cli {
std::unique_ptr<application::ports::ResourceDiscoveryProbe> probe(model::ResourceDestination destination)
{
    const auto config = resourceArchiveConfiguration();
    const auto &lane = config.resource_pipeline.targets.at(static_cast<std::size_t>(destination));
    if (destination == model::ResourceDestination::Local || !lane.enabled) throw std::invalid_argument("discovery cloud destination not enabled");
    const ResourceCloudTarget target {destination, lane.staging_path, lane.staging_max_bytes, lane.adapter, lane.rclone, {}};
    auto client = std::make_shared<http::client::AsioHttpClient>(makeHttpClientOptions(config));
    const auto identity = [config, target, client](const model::ResourceTransferContext &context) {
        using Result = application::result::ServiceResult<std::string>;
        const auto value = verifyCloudResourceTarget(config, target, context, client);
        if (!value.profile.available) return Result::failure(application::result::ServiceErrorType::ExternalApiError, "discovery_identity_unavailable");
        return Result::success(value.profile.fingerprint);
    };
    if (lane.adapter == "rclone") return storage::rclone::makeRcloneDiscoveryProbe(lane.rclone.value_or(config.rclone), client, identity);
    if (lane.adapter == "baidupcs" && destination == model::ResourceDestination::Baidu) return storage::baidupcs::makeBaiduDiscoveryProbe(config.baidupcs, {}, identity);
    throw std::invalid_argument("unsupported discovery adapter");
}
} // namespace orchestrator::modules::crawler::infrastructure::app::archive_cli
